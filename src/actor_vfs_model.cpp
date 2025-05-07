/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <vector>
#include <stdint.h>
#include <QTimer>
#include <QIcon>

#include "actor/service.h"
#include "actor/cxx_helper.h"
#include "actor_vfs_model.h"
#include "deconz/am_core.h"
#include "deconz/am_vfs.h"
#include "deconz/atom_table.h"
#include "deconz/dbg_trace.h"
#include "deconz/u_assert.h"
#include "deconz/u_sstream.h"
#include "deconz/u_memory.h"

#define AM_ACTOR_ID_UI_VFS      4006
#define AM_ACTOR_ID_OTA         9000

#define DIR_VALUE_INITIAL 0xDEADBEEF

static struct am_api_functions *am = nullptr;
static struct am_actor am_actor_vfs_model;

static AT_AtomIndex ati_type_dir;
static AT_AtomIndex ati_type_bool;
static AT_AtomIndex ati_type_u8;
static AT_AtomIndex ati_type_u16;
static AT_AtomIndex ati_type_u32;
static AT_AtomIndex ati_type_u64;
static AT_AtomIndex ati_type_i8;
static AT_AtomIndex ati_type_i16;
static AT_AtomIndex ati_type_i32;
static AT_AtomIndex ati_type_i64;
static AT_AtomIndex ati_type_blob;
static AT_AtomIndex ati_type_str;
static AT_AtomIndex ati_dot_actor;
static AT_AtomIndex ati_name;
static AT_AtomIndex ati_unknown;

static inline bool operator==(AT_AtomIndex a, AT_AtomIndex b)
{
    return a.index == b.index;
}

static inline bool operator!=(AT_AtomIndex a, AT_AtomIndex b)
{
    return a.index != b.index;
}

#define ENTRY_PARENT_NONE   -1
#define ENTRY_SIBLING_NONE  -3
#define ENTRY_CHILD_NONE    -4
#define ENTRY_CHILD_UNKNOWN -5

/*
 * The tree model is a vector where each entry can point to other entries
 * via index. A negative index means invalid.
 *
 * No raw pointers are used so resizing/growing the std::vector<Entry> is safe.
 */
struct Entry
{
    uint64_t value;
    AT_AtomIndex name;
    AT_AtomIndex type;
    int parent;
    int sibling;
    int child;
    uint32_t mode;
    uint16_t icon;
    // TODO remove for larger than 64-bit value heap objects
    uint8_t data[29];
};

static_assert(sizeof(Entry) == 64, "unexpected size");

enum EntryFetchState
{
    ENTRY_FETCH_STATE_DONE,
    ENTRY_FETCH_STATE_WAIT_START,
    ENTRY_FETCH_STATE_WAIT_RESPONSE
};

struct DirFetcher
{
    EntryFetchState state;
    int entryIndex;
    int timeout;
    uint32_t index;
    uint16_t tag;
};

struct EntryFetcher
{
    int entryIndex;
    int timeout;
    uint16_t tag;
    EntryFetchState state;
};


class ActorVfsModelPrivate
{
public:
    std::vector<Entry> entries;
    std::vector<DirFetcher> dirFetchers;
    std::vector<EntryFetcher> entryFetchers;
    uint16_t allocTag = 1;
    QTimer fetchTimer;

    QIcon iconActor;
    QIcon iconDirectory;
};

static ActorVfsModelPrivate *_priv = nullptr;
static ActorVfsModel *_instance = nullptr;

int findChildEntry(const std::vector<Entry> &entries, int parent_e, AT_AtomIndex name)
{
    if (parent_e < 0)
        return ENTRY_CHILD_NONE;

    if (parent_e >= (int)entries.size())
        return ENTRY_CHILD_NONE;

    int e = entries[parent_e].child;

    for (;e > 0;)
    {
        if (entries[e].name == name)
            return e;

        e = entries[e].sibling;
    }

    return ENTRY_CHILD_NONE;
}

static void addEntryToValueFetchers(int e)
{
    for (const EntryFetcher &ef : _priv->entryFetchers)
    {
        if (ef.entryIndex == e)
            return;
    }

    EntryFetcher ef;
    ef.state = ENTRY_FETCH_STATE_WAIT_START;
    ef.entryIndex = e;
    ef.tag = 0;
    ef.timeout = 0;

    _priv->entryFetchers.push_back(ef);
}

static void addEntryToParent(std::vector<Entry> &entries, int parent_e, Entry &entry)
{
    entries.push_back(entry);

    if (entries[parent_e].child < 0)
    {
        entries[parent_e].child = entries.size() - 1;
    }
    else
    {
        int e = entries[parent_e].child;

        for (; entries[e].sibling >= 0;)
        {
            e = entries[e].sibling;
        }

        entries[e].sibling = entries.size() - 1;
    }

    if (entry.type != ati_type_dir)
    {
        addEntryToValueFetchers(entries.size() - 1);
    }
    else
    {
        /* automatically fetch the .actor directory */
        if (entry.name == ati_dot_actor)
        {

            DirFetcher df;
            df.entryIndex = entries.size() - 1;
            df.index = 0;
            df.state = ENTRY_FETCH_STATE_WAIT_START;

            _priv->dirFetchers.push_back(df);
        }
    }
}

static void listDirectoryRequest(DirFetcher &df)
{
    int e = df.entryIndex;
    DBG_Assert(e >= 0);

    am_actor_id dstActorId;
    auto &entries = _priv->entries;
    Entry &entry = entries[e];

    {
        int ep = e;
        for (;_priv->entries[ep].parent >= 0;)
        {
            ep = _priv->entries[ep].parent;
        }

        dstActorId = _priv->entries[ep].value;
    }

    U_ASSERT(df.state == ENTRY_FETCH_STATE_WAIT_START);

    std::vector<int> path;

    for (;entries[e].parent >= 0;)
    {
        path.push_back(e);
        e = entries[e].parent;
    }

    char url[1024];
    url[0] = '\0';
    U_SStream ss;

    U_sstream_init(&ss, url, sizeof(url));

    while (!path.empty()) // can be empty for actors as root
    {
        e = path.back();
        path.pop_back();
        AT_Atom a = AT_GetAtomByIndex(entries[e].name);
        if (a.len)
        {
            U_sstream_put_str(&ss, (const char*)a.data);
            if (!path.empty())
                U_sstream_put_str(&ss, "/");
        }
    }

    am_message *m = am->msg_alloc();
    if (!m)
        return;

    DBG_Printf(DBG_VFS, "list directory request e: %d, %s\n", df.entryIndex, url);

    _priv->allocTag++;
    df.tag = _priv->allocTag;
    df.timeout = 0;

    am->msg_put_u16(m, df.tag);    /* tag */
    am->msg_put_cstring(m, url);   /* url */
    am->msg_put_u32(m, df.index);  /* index */
    am->msg_put_u32(m, 128);       /* max_count */
    m->src = AM_ACTOR_ID_UI_VFS;
    m->dst = dstActorId;
    m->id = VFS_M_ID_LIST_DIR_REQ;

    if (am->send_message(m))
        df.state = ENTRY_FETCH_STATE_WAIT_RESPONSE;

}

static int readEntryRequest(EntryFetcher &ef)
{
    int e = ef.entryIndex;

    if (e < 0)
        return 0;

    auto &entries = _priv->entries;
    Entry &entry = entries[e];

    U_ASSERT(ef.state == ENTRY_FETCH_STATE_WAIT_START);

    std::vector<int> path;

    for (;entries[e].parent >= 0;)
    {
        path.push_back(e);
        e = entries[e].parent;
    }

    am_actor_id actorId = entries[e].value;

    char url[1024];
    url[0] = '\0';
    U_SStream ss;

    U_sstream_init(&ss, url, sizeof(url));

    if (path.empty() && entry.parent < 0)
    {
        // special case query actors name
        U_sstream_put_str(&ss, ".actor/name");
    }
    else
    {
        while (!path.empty()) // can be empty for actors as root
        {
            e = path.back();
            path.pop_back();
            AT_Atom a = AT_GetAtomByIndex(entries[e].name);
            if (a.len)
            {
                U_sstream_put_str(&ss, (const char*)a.data);
                if (!path.empty())
                    U_sstream_put_str(&ss, "/");
            }
        }
    }

    am_message *m = am->msg_alloc();
    if (!m)
        return 0;

    DBG_Printf(DBG_VFS, "vfs model: fetch value of entry: %d, url: '%s'\n", e, url);

    _priv->allocTag++;
    ef.tag = _priv->allocTag;
    ef.state = ENTRY_FETCH_STATE_WAIT_RESPONSE;
    ef.timeout = 0;

    am->msg_put_u16(m, ef.tag);
    am->msg_put_cstring(m, url);
    m->src = AM_ACTOR_ID_UI_VFS;
    m->dst = actorId;
    m->id = VFS_M_ID_READ_ENTRY_REQ;

    if (am->send_message(m))
        return 1;

    ef.state = ENTRY_FETCH_STATE_WAIT_START;

    return 0;
}

int ActorVfsModel::listDirectoryResponse(am_message *msg)
{
    unsigned i;
    size_t fetcherIndex = 0;
    unsigned status;
    unsigned short tag;
    int entryIndex = -1;
    am_string name;
    unsigned flags;
    unsigned icon;
    unsigned index;
    unsigned next_index;
    unsigned count;

    tag = am->msg_get_u16(msg);
    status = am->msg_get_u8(msg);

    for (fetcherIndex = 0; fetcherIndex < _priv->dirFetchers.size(); fetcherIndex++)
    {
        if (_priv->dirFetchers[fetcherIndex].tag == tag)
        {
            entryIndex = _priv->dirFetchers[fetcherIndex].entryIndex;
            break;
        }
    }

    if (fetcherIndex == _priv->dirFetchers.size())
        return AM_CB_STATUS_OK;

    if (entryIndex < 0) // ?? should not happen
        return AM_CB_STATUS_OK;

    DirFetcher df = _priv->dirFetchers[fetcherIndex];

    if (df.state != ENTRY_FETCH_STATE_WAIT_RESPONSE)
        return AM_CB_STATUS_OK;

    _priv->dirFetchers[fetcherIndex] = _priv->dirFetchers.back();
    _priv->dirFetchers.pop_back();

    priv->fetchTimer.stop(); // clear for continuation

    if (status == AM_RESPONSE_STATUS_OK)
    {
        index = am->msg_get_u32(msg);
        next_index = am->msg_get_u32(msg);
        count = am->msg_get_u32(msg);

        if (msg->status != AM_MSG_STATUS_OK)
            return AM_CB_STATUS_INVALID;

        DBG_Printf(DBG_VFS, "vfs model: handle list directory rsp, tag: %u index: %u, next_index: %u, count: %u\n", tag, index, next_index, count);

        std::vector<Entry> entriesToAdd;

        for (i = 0; i < count; i++)
        {
            name = am->msg_get_string(msg);
            flags = am->msg_get_u16(msg);
            icon = am->msg_get_u16(msg);

            if (msg->status != AM_MSG_STATUS_OK)
                return AM_CB_STATUS_INVALID;

            if (name.size == 0)
                continue;

            AT_AtomIndex ati_name;
            AT_AddAtom(name.data, name.size, &ati_name);

            int e = findChildEntry(_priv->entries, entryIndex, ati_name);

            if (e < 0)
            {
                Entry entry;
                entry.name = ati_name;
                if (flags & VFS_LS_DIR_ENTRY_FLAGS_IS_DIR)
                {
                    entry.type = ati_type_dir;
                    entry.value = DIR_VALUE_INITIAL;
                }
                else
                {
                    entry.type = ati_unknown;
                    entry.value = 0;
                }
                entry.parent = entryIndex;
                entry.sibling = ENTRY_SIBLING_NONE;
                entry.child = ENTRY_CHILD_UNKNOWN;

                entry.mode = 0;
                entry.icon = icon;
                entriesToAdd.push_back(entry);
            }
            else
            {
                // TODO mark as found. Entries which aren't marked should be removed in the end.
                const Entry &entry = _priv->entries[e];
                if (entry.type != ati_type_dir)
                    addEntryToValueFetchers(e);
            }

            DBG_Printf(DBG_VFS, "             %.*s\n", name.size, name.data);
        }

        if (!entriesToAdd.empty())
        {
            // figure out parent index
            int row = 0;
            int e = entryIndex;
            int parent_e = priv->entries[e].parent;

            if (parent_e < 0) // top level entry
            {
                parent_e = 0;
            }
            else
            {
                parent_e = priv->entries[parent_e].child;
            }

            for (; parent_e >= 0; )
            {
                if (parent_e == e)
                    break;

                row++;
                parent_e = priv->entries[parent_e].sibling;
            }

            QModelIndex parent = createIndex(row, 0, (quintptr)e);
            int first = 0;
            int last = 0;

            int child = priv->entries[e].child;
            for (;child > 0; first++)
            {
                child = priv->entries[child].sibling;
            }
            last = first + (int)entriesToAdd.size() - 1;

            DBG_Printf(DBG_VFS, "vfs model: insert rows e: %d, parent_e: %d, row: %d, first: %d, last: %d\n", e, parent_e, row, first, last);

            beginInsertRows(parent, first, last);

            for (size_t j = 0; j < entriesToAdd.size(); j++)
            {
                addEntryToParent(_priv->entries, entryIndex, entriesToAdd[j]);
            }

            endInsertRows();
        }

        if (next_index == 0)
        {
            // done
        }
        else
        {
            // no errors so far, continue
            df.state = ENTRY_FETCH_STATE_WAIT_START;
            df.index = next_index;
            priv->dirFetchers.push_back(df);
        }
    }
    else
    {
        DBG_Printf(DBG_VFS, "vfs model: list directory error: %u\n", status);
    }

    return AM_CB_STATUS_OK;
}

int ActorVfsModel::readEntryResponse(am_message *msg)
{
    int e;
    unsigned status;
    unsigned short tag;

    am_string type;
    unsigned mode;
    uint64_t mtime;
    int fetchIter;

    tag = am->msg_get_u16(msg);
    status = am->msg_get_u8(msg);

    for (fetchIter = 0; fetchIter < priv->entryFetchers.size(); fetchIter++)
    {
        if (priv->entryFetchers[fetchIter].tag == tag)
            break;
    }

    if (fetchIter == priv->entryFetchers.size())
        return AM_CB_STATUS_OK;

    {
        EntryFetcher ef = priv->entryFetchers[fetchIter];
        priv->entryFetchers[fetchIter] = priv->entryFetchers.back();
        priv->entryFetchers.pop_back();

        if (ef.state == ENTRY_FETCH_STATE_WAIT_RESPONSE)
            priv->fetchTimer.stop(); // clear for continuation

        e = ef.entryIndex;
        if (e < 0)
            return AM_CB_STATUS_OK;
    }

    Entry &entry = priv->entries[e];

    if (status == AM_RESPONSE_STATUS_OK && msg->status == AM_MSG_STATUS_OK)
    {
        type = am->msg_get_string(msg);
        mode = am->msg_get_u32(msg);
        mtime = am->msg_get_u64(msg);
        (void)mtime;

        AT_AtomIndex ati_type = ati_unknown;
        if (type.size)
        {
            if (0 == AT_GetAtomIndex(type.data, type.size, &ati_type))
                ati_type = ati_unknown;
        }

        if (msg->status == AM_MSG_STATUS_OK && type.size)
        {
            entry.mode = mode;
            entry.type = ati_type;

            if      (type == "bool") { entry.value = am->msg_get_u8(msg); }
            else if (type == "u8")   { entry.value = am->msg_get_u8(msg); }
            else if (type == "u16")  { entry.value = am->msg_get_u16(msg); }
            else if (type == "u32")  { entry.value = am->msg_get_u32(msg); }
            else if (type == "u64")  { entry.value = am->msg_get_u64(msg); }
            else if (type == "i8")   { entry.value = am->msg_get_s8(msg); }
            else if (type == "i16")  { entry.value = am->msg_get_s16(msg); }
            else if (type == "i32")  { entry.value = am->msg_get_s32(msg); }
            else if (type == "i64")  { entry.value = am->msg_get_s64(msg); }
            else if (type == "str")
            {
                am_string str = am->msg_get_string(msg);
                entry.value = str.size;
                if (entry.value > sizeof(entry.data))
                    entry.value = sizeof(entry.data);

                for (unsigned i = 0; i < entry.value; i++)
                {
                    entry.data[i] = (uint8_t)str.data[i];
                }

                // special case: .actor/name in root entry
                U_ASSERT(entry.parent >= 0);
                if (entry.name == ati_name && priv->entries[entry.parent].name == ati_dot_actor)
                {
                    int ppp = priv->entries[entry.parent].parent;
                    Entry &actorEntry = priv->entries[ppp];
                    if (actorEntry.name == ati_unknown)
                    {
                        AT_AddAtom(str.data, str.size, &actorEntry.name);

                        QModelIndex index = createIndex(ppp, 0, (quintptr)ppp);
                        emit dataChanged(index, index);
                    }
                }
            }
            else if (type == "blob")
            {
                am_blob blob = am->msg_get_blob(msg);
                entry.value = blob.size;
                if (entry.value > sizeof(entry.data))
                    entry.value = sizeof(entry.data);

                for (unsigned i = 0; i < entry.value; i++)
                {
                    entry.data[i] = blob.data[i];
                }
            }
            else
            {
                DBG_Printf(DBG_VFS, "vfs model: read entry rsp: TODO handle type\n");
            }

            DBG_Printf(DBG_VFS, "vfs model: read entry rsp: type: %.*s, value: %llu\n", type.size, type.data, (unsigned long long)entry.value);

            {
                int parent_e = priv->entries[e].parent;

                if (parent_e < 0)
                    parent_e = 0;

                int row = 0;
                int child = priv->entries[parent_e].child;
                for (; child > 0; )
                {
                    if (child == e)
                        break;

                    row++;
                    child = priv->entries[child].sibling;
                }

                int column = 1; // type;
                QModelIndex index = createIndex(row, column, (quintptr)e);
                column = 2; // value;
                QModelIndex index2 = createIndex(row, column, (quintptr)e);
                emit dataChanged(index, index2);
            }

            return AM_CB_STATUS_OK;
        }
    }
    else
    {
        // if this is a actor entry assume it doesn't export a vfs
        if (entry.parent == ENTRY_PARENT_NONE)
        {
            if (entry.child == ENTRY_CHILD_UNKNOWN)
            {
                entry.child = ENTRY_CHILD_NONE;
            }
        }
    }

    DBG_Printf(DBG_VFS, "vfs model: read entry: %d response error, tag: %u, status: %u\n", e, tag, status);
    return AM_CB_STATUS_OK;
}

void ActorVfsModel::continueFetching()
{
    if (!priv->dirFetchers.empty())
    {
        DirFetcher &df = priv->dirFetchers.front();

        if (df.state == ENTRY_FETCH_STATE_WAIT_START)
        {
            listDirectoryRequest(df);
            if (df.state == ENTRY_FETCH_STATE_WAIT_RESPONSE)
            {
                priv->fetchTimer.start(50);
            }
        }
    }
    else if (!priv->entryFetchers.empty())
    {
        EntryFetcher &ef = priv->entryFetchers.front();

        if (ef.state == ENTRY_FETCH_STATE_WAIT_START)
        {
            readEntryRequest(ef);
            if (ef.state == ENTRY_FETCH_STATE_WAIT_RESPONSE)
            {
                priv->fetchTimer.start(50);
            }
        }
    }
}

void ActorVfsModel::addActorId(unsigned int actorId)
{
    int e = 0;
    int prev_e = -1;

    if (!priv->entries.empty())
    {
        for (e = 0; e >= 0;)
        {
            DBG_Assert(priv->entries[e].parent == ENTRY_PARENT_NONE);
            if (priv->entries[e].value == actorId)
                return; // already registered

            prev_e = e;
            e = priv->entries[e].sibling;
        }
    }

    Entry entry;

    entry.name = ati_unknown;
    entry.value = actorId;
    entry.mode = 0;
    entry.icon = 0;
    entry.type = ati_type_dir;
    entry.parent = ENTRY_PARENT_NONE;
    entry.sibling = ENTRY_SIBLING_NONE;
    entry.child = ENTRY_CHILD_UNKNOWN;

    priv->entries.push_back(entry);

    e = (int)priv->entries.size() - 1;

    if (prev_e >= 0)
    {
        priv->entries[prev_e].sibling = e;
    }

    DirFetcher df;
    df.entryIndex = e;
    df.index = 0;
    df.state = ENTRY_FETCH_STATE_WAIT_START;

    priv->dirFetchers.push_back(df);

    continueFetching();
}

void ActorVfsModel::fetchTimerFired()
{
    DBG_Printf(DBG_VFS, "vfs timer fired after %d, dirf: %zu, entryf: %zu\n", priv->fetchTimer.interval(), priv->dirFetchers.size(), priv->entryFetchers.size());

    if (!priv->dirFetchers.empty())
    {
        DirFetcher &df = priv->dirFetchers.front();

        if (df.state == ENTRY_FETCH_STATE_WAIT_RESPONSE)
        {
            df.timeout++;
            if (df.timeout < 3)
            {
                df.state = ENTRY_FETCH_STATE_WAIT_START;
            }
            else
            {
                priv->dirFetchers.front() = priv->dirFetchers.back();
                priv->dirFetchers.pop_back();
            }
            continueFetching();
        }
    }
    else if (!priv->entryFetchers.empty())
    {
        EntryFetcher &ef = priv->entryFetchers.front();

        if (ef.state == ENTRY_FETCH_STATE_WAIT_RESPONSE)
        {
            ef.timeout++;
            if (ef.timeout < 3)
            {
                ef.state = ENTRY_FETCH_STATE_WAIT_START;
            }
            else
            {
                priv->entryFetchers.front() = priv->entryFetchers.back();
                priv->entryFetchers.pop_back();
            }
            continueFetching();
        }
    }
}

static int VfsModel_MessageCallback(struct am_message *msg)
{
    int ret = AM_CB_STATUS_UNSUPPORTED;
    if (msg->id == VFS_M_ID_READ_ENTRY_RSP)
    {
        ret = _instance->readEntryResponse(msg);
        _instance->continueFetching();
    }
    else if (msg->id == VFS_M_ID_LIST_DIR_RSP)
    {
        ret = _instance->listDirectoryResponse(msg);
        _instance->continueFetching();
    }

    return ret;
}


ActorVfsModel::ActorVfsModel(QObject *parent) :
    QAbstractItemModel{parent},
    priv(new ActorVfsModelPrivate)
{
    _priv = priv;
    _instance = this;

    priv->iconActor.addFile(":/icons/cryo/32/drive-disk.png");
    priv->iconDirectory.addFile(":/icons/cryo/32/folder.png");

    const char *str;

    str = ".actor";
    AT_AddAtom(str, U_strlen(str), &ati_dot_actor);
    str = "name";
    AT_AddAtom(str, U_strlen(str), &ati_name);
    str = "dir";
    AT_AddAtom(str, U_strlen(str), &ati_type_dir);
    str = "bool";
    AT_AddAtom(str, U_strlen(str), &ati_type_bool);
    str = "u8";
    AT_AddAtom(str, U_strlen(str), &ati_type_u8);
    str = "u16";
    AT_AddAtom(str, U_strlen(str), &ati_type_u16);
    str = "u32";
    AT_AddAtom(str, U_strlen(str), &ati_type_u32);
    str = "u64";
    AT_AddAtom(str, U_strlen(str), &ati_type_u64);
    str = "i8";
    AT_AddAtom(str, U_strlen(str), &ati_type_i8);
    str = "i16";
    AT_AddAtom(str, U_strlen(str), &ati_type_i16);
    str = "i32";
    AT_AddAtom(str, U_strlen(str), &ati_type_i32);
    str = "i64";
    AT_AddAtom(str, U_strlen(str), &ati_type_i64);
    str = "str";
    AT_AddAtom(str, U_strlen(str), &ati_type_str);
    str = "blob";
    AT_AddAtom(str, U_strlen(str), &ati_type_blob);
    str = "unknown";
    AT_AddAtom(str, U_strlen(str), &ati_unknown);

    AM_INIT_ACTOR(&am_actor_vfs_model, AM_ACTOR_ID_UI_VFS, VfsModel_MessageCallback);
    am = AM_ApiFunctions();
    am->register_actor(&am_actor_vfs_model);

    addActorId(AM_ACTOR_ID_CORE_NET);
    addActorId(AM_ACTOR_ID_CORE_APS);
    addActorId(4001); //  plugin test
    //addActorId(AM_ACTOR_ID_OTA);

    connect(&priv->fetchTimer, &QTimer::timeout, this, &ActorVfsModel::fetchTimerFired);
    priv->fetchTimer.setSingleShot(true);
}

ActorVfsModel::~ActorVfsModel()
{
    am->unregister_actor(&am_actor_vfs_model);
    delete priv;
    priv = nullptr;
    _priv = nullptr;
    _instance = nullptr;
}

QVariant ActorVfsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
    {
        return QVariant();
    }

    int e = (int)index.internalId();
    if (e < 0)
        return QVariant();

    const Entry &entry = priv->entries[e];

    if  (role == Qt::DisplayRole)
    {
//        DBG_Printf(DBG_VFS, "vfs model: data(row: %d, column: %d) id: %d, role: display\n", index.row(), index.column(), (int)index.internalId());

        AT_Atom a;
        a.len = 0;

        if (index.column() == ColumnName)
        {
            a = AT_GetAtomByIndex(entry.name);
        }
        else if (index.column() == ColumnType)
        {
            if (entry.parent == ENTRY_PARENT_NONE)
                return QLatin1String("actor");

            a = AT_GetAtomByIndex(entry.type);
        }
        else if (index.column() == ColumnValue)
        {
            if (entry.parent > 0)
            {
                if (entry.type == ati_type_dir)
                    return QVariant();

                unsigned display = (entry.mode & 0xF0000);

                if (display == VFS_ENTRY_MODE_DISPLAY_HEX)
                {
                    if (entry.type == ati_type_u8)
                        return QString("0x%1").arg(entry.value, 2, 16, QLatin1Char('0'));
                    if (entry.type == ati_type_u16)
                        return QString("0x%1").arg(entry.value, 4, 16, QLatin1Char('0'));
                    if (entry.type == ati_type_u32)
                        return QString("0x%1").arg(entry.value, 8, 16, QLatin1Char('0'));
                    if (entry.type == ati_type_u64)
                        return QString("0x%1").arg(entry.value, 16, 16, QLatin1Char('0'));
                }

                if (entry.type == ati_type_str)
                {
                    if (entry.value && entry.value <= sizeof(entry.data))
                        return QString::fromUtf8((const char*)entry.data, entry.value);
                }
                else if (entry.type == ati_type_bool)
                {
                    return entry.value != 0;
                }
                else if (entry.type == ati_type_blob)
                {
                    if (entry.value && entry.value <= sizeof(entry.data))
                        return QLatin1String("0x") + QByteArray::fromRawData((const char*)entry.data, entry.value).toHex();
                }
                else if (entry.type == ati_type_u64)
                {
                    return (qulonglong)entry.value;
                }

                return (qlonglong)entry.value;
            }
            else
            {
                // actor
                return (quint32)entry.value;
            }
        }

        if (a.len)
        {
            return QString::fromUtf8((const char*)a.data, a.len);
        }
    }
    else if  (role == Qt::DecorationRole)
    {
        if (index.column() == ColumnName)
        {
            if (entry.parent == ENTRY_PARENT_NONE)
                return priv->iconActor;

            if (entry.type == ati_type_dir)
                return priv->iconDirectory;
        }
    }

    return QVariant();
}

QModelIndex ActorVfsModel:: indexWithName(unsigned atomIndex, const QModelIndex &parent) const
{
    if (priv->entries.empty())
        return QModelIndex();

    int e;

    if (parent.isValid())
    {
        e = (int)parent.internalId();
        U_ASSERT(0 < e);
        U_ASSERT(e < (int)priv->entries.size());
        e = priv->entries[e].child;
    }
    else
    {
        e = 0;
        U_ASSERT(priv->entries[e].parent == ENTRY_PARENT_NONE);
    }

    if (e < 0)
        return QModelIndex();

    int row = 0;
    int column = 0;
    for (; e >= 0;)
    {
        U_ASSERT(e < (int)priv->entries.size());
        const Entry &entry = priv->entries[e];

        if (entry.name.index == atomIndex)
            return createIndex(row, column, (quintptr)e);

        if (entry.sibling < 0)
        {
            U_ASSERT(entry.sibling == ENTRY_SIBLING_NONE);
            break;
        }

        row++;
        e = entry.sibling;
    }

    return QModelIndex();
}

QModelIndex ActorVfsModel::index(int row, int column, const QModelIndex &parent) const
{
    const auto &entries = priv->entries;

    if (entries.empty())
        return QModelIndex();

    if (!parent.isValid())
    {
        int e = 0;

        for (int i = 0; i < row; i++)
        {
            U_ASSERT(e < entries.size());
            int sibling = entries[e].sibling;
            U_ASSERT(sibling < (int)entries.size());
            if (sibling >= 0)
            {
                e = sibling;
            }
            else
            {
                return QModelIndex();
            }
        }

        return createIndex(row, column, (quintptr)e);
    }
    else
    {
        int e = (int)parent.internalId();
        if (e < 0)
            return QModelIndex();

        e = entries[e].child;
        if (e < 0)
            return QModelIndex();

        for (int i = 0; i < row; i++)
        {
            int sibling = entries[e].sibling;
            if (sibling >= 0)
            {
                e = sibling;
            }
            else
            {
                return QModelIndex();
            }
        }

        return createIndex(row, column, (quintptr)e);
    }

    return QModelIndex();
}

QModelIndex ActorVfsModel::parent(const QModelIndex &index) const
{
    int e = (int)index.internalId();

    if (e > 0 && index.isValid())
    {
        e = priv->entries[e].parent;
        if (e < 0)
            return QModelIndex();

        int parent_e = priv->entries[e].parent;

        if (parent_e < 0)
            parent_e = 0;

        int row = 0;
        for (row = 0; priv->entries[parent_e].sibling > 0; row++)
        {
            if (parent_e == e)
                break;

            parent_e = priv->entries[parent_e].sibling;
        }

        return createIndex(row, 0, (quintptr)e);
    }

    return QModelIndex();
}

int ActorVfsModel::rowCount(const QModelIndex &parent) const
{
    int i = 0;
    const auto &entries = priv->entries;

    if (entries.empty())
        return 0;

    if (parent.isValid())
    {
        int e = (int)parent.internalId();
        if (e < 0 || e >= (int)entries.size())
            return 0;

        const Entry &entry = entries[e];
        if (entry.type != ati_type_dir)
            return 0;

        e = entries[e].child;
        if (e > 0)
        {
            i = 1;
            for (; entries[e].sibling != ENTRY_SIBLING_NONE; i++)
                e = entries[e].sibling;
        }

        if (e == ENTRY_CHILD_UNKNOWN)
            i = 1; // hack: means unfetched
    }
    else
    {
        i = 1;
        for (int e = 0; entries[e].sibling != ENTRY_SIBLING_NONE; i++)
        {
            U_ASSERT(0 < entries[e].sibling);
            U_ASSERT(entries[e].sibling < entries.size());
            e = entries[e].sibling;
        }

    }

    return i;
}

int ActorVfsModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return ColumnMax;

    if (!priv->entries.empty())
        return ColumnMax;

    return 0;
}

bool ActorVfsModel::canFetchMore(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return false;

    int e = parent.internalId();
    const Entry &entry = priv->entries[e];

    if (entry.type == ati_type_dir)
    {
        if (entry.value == DIR_VALUE_INITIAL)
            return true;
    }

    return false;
}

void ActorVfsModel::fetchMore(const QModelIndex &parent)
{
    if (!parent.isValid())
        return;

    int e = (int)parent.internalId();

    if (e < 0)
        return;

    Entry &entry = priv->entries[e];

    if (entry.type == ati_type_dir)
    {
        for (DirFetcher &df : priv->dirFetchers)
        {
            if (df.entryIndex == e)
                return;
        }

        entry.value = DIR_VALUE_INITIAL + 1; // prevent canFetchMore

        DirFetcher df;

        df.entryIndex = e;
        df.index = 0;
        df.state = ENTRY_FETCH_STATE_WAIT_START;

        priv->dirFetchers.push_back(df);
        continueFetching();
    }
}

QVariant ActorVfsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        if      (section == ColumnName) return QLatin1String("Name");
        else if (section == ColumnType) return QLatin1String("Type");
        else if (section == ColumnValue) return QLatin1String("Value");
    }

    return QVariant();
}
