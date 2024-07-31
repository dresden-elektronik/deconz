/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
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
#include "deconz/atom_table.h"
#include "deconz/dbg_trace.h"
#include "deconz/u_sstream.h"

#define AM_ACTOR_ID_CORE_APS    2005
#define AM_ACTOR_ID_CORE_NET    2006
#define AM_ACTOR_ID_UI_VFS      4006
#define AM_ACTOR_ID_OTA         9000

/* Bit 0 Access */
#define AM_ENTRY_MODE_READONLY 0
#define AM_ENTRY_MODE_WRITEABLE 1

/* Bit 16-19 Display */
#define AM_ENTRY_MODE_DISPLAY_AUTO (0U << 16)
#define AM_ENTRY_MODE_DISPLAY_HEX  (1U << 16)
#define AM_ENTRY_MODE_DISPLAY_BIN  (2U << 16)

enum CommonMessageIds
{
   M_ID_LIST_DIR_REQ = AM_MESSAGE_ID_COMMON_REQUEST(1),
   M_ID_LIST_DIR_RSP = AM_MESSAGE_ID_COMMON_RESPONSE(1),
   M_ID_READ_ENTRY_REQ = AM_MESSAGE_ID_COMMON_REQUEST(2),
   M_ID_READ_ENTRY_RSP = AM_MESSAGE_ID_COMMON_RESPONSE(2)
};

static struct am_api_functions *am = nullptr;
static struct am_actor am_actor_vfs_model;

static AT_AtomIndex ati_type_dir;
static AT_AtomIndex ati_type_u8;
static AT_AtomIndex ati_type_u16;
static AT_AtomIndex ati_type_u32;
static AT_AtomIndex ati_type_u64;
static AT_AtomIndex ati_type_blob;
static AT_AtomIndex ati_type_str;
static AT_AtomIndex ati_unknown;

static inline bool operator==(AT_AtomIndex a, AT_AtomIndex b)
{
    return a.index == b.index;
}

static inline bool operator!=(AT_AtomIndex a, AT_AtomIndex b)
{
    return a.index != b.index;
}

enum EntryFetchState
{
    ENTRY_FETCH_STATE_IDLE,
    ENTRY_FETCH_STATE_WAIT_START,
    ENTRY_FETCH_STATE_WAIT_RESPONSE
};

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
    uint32_t mode;
    AT_AtomIndex name;
    AT_AtomIndex type;
    int parent;
    int sibling;
    int child;
    uint8_t fetchState; // EntryFetchState
    uint8_t data[24];
};

static_assert(sizeof(Entry) == 64, "unexpected size");

struct DirFetcher
{
    am_actor_id actorId;
    int entryIndex;
    uint16_t tag;
    uint32_t index;
    uint64_t timeout;
};

enum ValueFetchState
{
    VAL_FETCH_STATE_IDLE,
    VAL_FETCH_STATE_WAIT_TIMER,
    VAL_FETCH_STATE_WAIT_RESPONSE
};

class ActorVfsModelPrivate
{
public:

    std::vector<Entry> entries;
    std::vector<DirFetcher> dirFetchers;
    std::vector<int> valueFetchers;
    uint16_t fetchTag = 1;
    ValueFetchState valFetchState = VAL_FETCH_STATE_IDLE;
    unsigned fetchIter = 0;
    uint16_t valFetchTag = 1;
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
    for (size_t i = 0; i < _priv->valueFetchers.size(); i++)
    {
        if (_priv->valueFetchers[i] == e)
            return;
    }

    _priv->valueFetchers.push_back(e);
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
        addEntryToValueFetchers(entries.size() - 1);
}

static void listDirectoryRequest(DirFetcher &fetch)
{
    int e = fetch.entryIndex;
    DBG_Assert(e >= 0);

    auto &entries = _priv->entries;
    Entry &entry = entries[e];

    DBG_Assert(entry.fetchState == ENTRY_FETCH_STATE_WAIT_START);

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

    DBG_Printf(DBG_VFS, "list directory request e: %d, %s\n", fetch.entryIndex, url);

    entry.fetchState = ENTRY_FETCH_STATE_WAIT_RESPONSE;
    fetch.tag = _priv->fetchTag++;

    am->msg_put_u16(m, fetch.tag);    /* tag */
    am->msg_put_cstring(m, url);      /* url */
    am->msg_put_u32(m, fetch.index);  /* index */
    m->src = AM_ACTOR_ID_UI_VFS;
    m->dst = fetch.actorId;
    m->id = M_ID_LIST_DIR_REQ;
    am->send_message(m);
}

static int readEntryRequest(int e)
{
    if (e < 0)
        return 0;

    auto &entries = _priv->entries;
    Entry &entry = entries[e];

    DBG_Assert(entry.fetchState == ENTRY_FETCH_STATE_IDLE);

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

    entry.fetchState = ENTRY_FETCH_STATE_WAIT_RESPONSE;
    _priv->valFetchTag++;

    am->msg_put_u16(m, _priv->valFetchTag);
    am->msg_put_cstring(m, url);
    m->src = AM_ACTOR_ID_UI_VFS;
    m->dst = actorId;
    m->id = M_ID_READ_ENTRY_REQ;

    if (am->send_message(m))
        return 1;

    return 0;
}

int ActorVfsModel::listDirectoryResponse(am_message *msg)
{
    unsigned i;
    size_t fetcherIndex = 0;
    unsigned status;
    unsigned short tag;
    int entryIndex = -1;
    am_string url;
    am_string name;
    am_string type;
    unsigned mode;
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

    DBG_Assert(_priv->entries[entryIndex].fetchState == ENTRY_FETCH_STATE_WAIT_RESPONSE);
    _priv->entries[entryIndex].fetchState = ENTRY_FETCH_STATE_IDLE;

    if (status == AM_RESPONSE_STATUS_OK)
    {
        url = am->msg_get_string(msg);
        index = am->msg_get_u32(msg);
        next_index = am->msg_get_u32(msg);
        count = am->msg_get_u32(msg);

        if (next_index == 0)
        {
            // done
            _priv->dirFetchers[fetcherIndex] = _priv->dirFetchers.back();
            _priv->dirFetchers.pop_back();
        }
        else
        {
            _priv->entries[entryIndex].fetchState = ENTRY_FETCH_STATE_WAIT_START;
            _priv->dirFetchers[fetcherIndex].index = next_index;
            listDirectoryRequest(_priv->dirFetchers[fetcherIndex]);
        }

        if (msg->status != AM_MSG_STATUS_OK)
            return AM_CB_STATUS_INVALID;

        DBG_Printf(DBG_VFS, "vfs model: handle list directory rsp, tag: %u url: %.*s, index: %u, next_index: %u, count: %u\n", tag, url.size, url.data, index, next_index, count);

        std::vector<Entry> entriesToAdd;

        for (i = 0; i < count; i++)
        {
            name = am->msg_get_string(msg);
            type = am->msg_get_string(msg);
            mode = am->msg_get_u32(msg);

            if (msg->status != AM_MSG_STATUS_OK)
                return AM_CB_STATUS_INVALID;

            if (name.size == 0 || type.size == 0)
                continue;

            AT_AtomIndex ati_name;
            AT_AddAtom(name.data, name.size, &ati_name);

            AT_AtomIndex ati_type;
            AT_AddAtom(type.data, type.size, &ati_type);

            int e = findChildEntry(_priv->entries, entryIndex, ati_name);

            if (e < 0)
            {
                Entry entry;
                entry.name = ati_name;
                entry.type = ati_type;
                entry.parent = entryIndex;
                entry.sibling = ENTRY_SIBLING_NONE;
                entry.child = ENTRY_CHILD_UNKNOWN;
                entry.value = 0;
                entry.mode = mode;
                entry.fetchState = ENTRY_FETCH_STATE_IDLE;
                entriesToAdd.push_back(entry);
            }
            else
            {
                const Entry &entry = _priv->entries[e];
                if (entry.type != ati_type_dir)
                    addEntryToValueFetchers(e);
            }

            DBG_Printf(DBG_VFS, "             %.*s (%.*s) mode: %u\n", name.size, name.data, type.size, type.data, mode);
        }

        if (!entriesToAdd.empty())
        {
            // figure out parent index
            int e = entryIndex;
            int parent_e = priv->entries[e].parent;

            if (parent_e < 0)
                parent_e = 0;

            int row = 0;
            parent_e = priv->entries[parent_e].child;
            for (; parent_e > 0; )
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

        continueValueFetching();
    }
    else
    {
        DBG_Printf(DBG_VFS, "vfs model: list directory error: %u\n", status);
        _priv->dirFetchers[fetcherIndex] = _priv->dirFetchers.back();
        _priv->dirFetchers.pop_back();
    }

    return AM_CB_STATUS_OK;
}

int ActorVfsModel::readEntryResponse(am_message *msg, int e)
{
    if (e < 0)
        return AM_CB_STATUS_OK;

    Entry &entry = priv->entries[e];

    unsigned status;
    unsigned short tag;

    am_string url;
    am_string type;
    unsigned mode;
    uint64_t mtime;

    tag = am->msg_get_u16(msg);
    status = am->msg_get_u8(msg);

    if (entry.fetchState == ENTRY_FETCH_STATE_WAIT_RESPONSE)
        entry.fetchState = ENTRY_FETCH_STATE_IDLE;

    if (status == AM_RESPONSE_STATUS_OK && msg->status == AM_MSG_STATUS_OK)
    {
        if (tag != priv->valFetchTag)
            return AM_CB_STATUS_OK;

        url = am->msg_get_string(msg);
        type = am->msg_get_string(msg);
        mode = am->msg_get_u32(msg);
        mtime = am->msg_get_u64(msg);

        if (msg->status == AM_MSG_STATUS_OK && url.size && type.size)
        {
            entry.mode = mode;

            if      (type == "u8")  { entry.value = am->msg_get_u8(msg); }
            else if (type == "u16") { entry.value = am->msg_get_u16(msg); }
            else if (type == "u32") { entry.value = am->msg_get_u32(msg); }
            else if (type == "u64") { entry.value = am->msg_get_u64(msg); }
            else if (entry.parent < 0 && type == "str")
            {
                // special case: actor name in root entry
                if (url == ".actor/name")
                {
                    am_string str = am->msg_get_string(msg);
                    AT_AddAtom(str.data, str.size, &entry.name);
                }
            }
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

            DBG_Printf(DBG_VFS, "vfs model: read entry rsp: url: %.*s, type: %.*s, value: %llu\n", url.size, url.data, type.size, type.data, (unsigned long long)entry.value);

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

                int column = 2; // value;
                QModelIndex index = createIndex(row, column, (quintptr)e);
                emit dataChanged(index, index);
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

void ActorVfsModel::continueValueFetching()
{
    if (priv->valFetchState == VAL_FETCH_STATE_IDLE)
    {
        priv->valFetchState = VAL_FETCH_STATE_WAIT_TIMER;
        priv->fetchTimer.start(0);
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
    entry.type = ati_type_dir;
    entry.parent = ENTRY_PARENT_NONE;
    entry.sibling = ENTRY_SIBLING_NONE;
    entry.child = ENTRY_CHILD_UNKNOWN;
    entry.fetchState = ENTRY_FETCH_STATE_IDLE;
    priv->entries.push_back(entry);

    e = (int)priv->entries.size() - 1;

    if (prev_e >= 0)
    {
        priv->entries[prev_e].sibling = e;
    }

    addEntryToValueFetchers(e);
    continueValueFetching();
}

void ActorVfsModel::fetchTimerFired()
{
    DBG_Assert(priv->valFetchState == VAL_FETCH_STATE_WAIT_TIMER);
    priv->valFetchState = VAL_FETCH_STATE_IDLE;

    if (priv->valueFetchers.empty())
        return;

    if (priv->fetchIter >= priv->valueFetchers.size())
    {
        priv->valueFetchers.clear();
        priv->fetchIter = 0;
        return;
    }

    int e = priv->valueFetchers[priv->fetchIter];
    Entry &entry = priv->entries[e];

    if (entry.type == ati_type_dir && entry.parent >= 0)
    {
        priv->fetchIter++;
        continueValueFetching();
        return;
    }

    if (entry.fetchState == ENTRY_FETCH_STATE_IDLE)
    {
        if (readEntryRequest(e))
        {
            priv->valFetchState = VAL_FETCH_STATE_WAIT_RESPONSE;
        }
        else
        {
            priv->fetchIter++;
            continueValueFetching();
        }
    }
}

static int VfsModel_MessageCallback(struct am_message *msg)
{
    if (msg->id == M_ID_READ_ENTRY_RSP)
    {
        if (_priv->valFetchState == VAL_FETCH_STATE_WAIT_RESPONSE)
        {
            int e = _priv->valueFetchers[_priv->fetchIter];
            _priv->fetchIter++;
            _priv->valFetchState = VAL_FETCH_STATE_IDLE;
            _instance->continueValueFetching();
            return _instance->readEntryResponse(msg, e);
        }
    }
    else if (msg->id == M_ID_LIST_DIR_RSP)
    {
        return _instance->listDirectoryResponse(msg);
    }

    return AM_CB_STATUS_UNSUPPORTED;
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

    str = "dir";
    AT_AddAtom(str, qstrlen(str), &ati_type_dir);
    str = "u8";
    AT_AddAtom(str, qstrlen(str), &ati_type_u8);
    str = "u16";
    AT_AddAtom(str, qstrlen(str), &ati_type_u16);
    str = "u32";
    AT_AddAtom(str, qstrlen(str), &ati_type_u32);
    str = "u64";
    AT_AddAtom(str, qstrlen(str), &ati_type_u64);
    str = "str";
    AT_AddAtom(str, qstrlen(str), &ati_type_str);
    str = "blob";
    AT_AddAtom(str, qstrlen(str), &ati_type_blob);
    str = "unknown";
    AT_AddAtom(str, qstrlen(str), &ati_unknown);

    addActorId(AM_ACTOR_ID_CORE_NET);
    addActorId(AM_ACTOR_ID_CORE_APS);
    addActorId(AM_ACTOR_ID_OTA);

    AM_INIT_ACTOR(&am_actor_vfs_model, AM_ACTOR_ID_UI_VFS, VfsModel_MessageCallback);
    am = AM_ApiFunctions();
    am->register_actor(&am_actor_vfs_model);

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

        if (index.column() == 0)
        {
            a = AT_GetAtomByIndex(entry.name);
        }
        else if (index.column() == 1)
        {
            if (entry.parent == ENTRY_PARENT_NONE)
                return QLatin1String("actor");

            a = AT_GetAtomByIndex(entry.type);
        }
        else if (index.column() == 2)
        {
            if (entry.parent > 0)
            {
                unsigned display = (entry.mode & 0xF0000);

                if (display == AM_ENTRY_MODE_DISPLAY_HEX)
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
                else if (entry.type == ati_type_blob)
                {
                    if (entry.value && entry.value <= sizeof(entry.data))
                        return QLatin1String("0x") + QByteArray::fromRawData((const char*)entry.data, entry.value).toHex();
                }

                return (qulonglong)entry.value;
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
        if (index.column() == 0)
        {
            if (entry.parent == ENTRY_PARENT_NONE)
                return priv->iconActor;

            if (entry.type == ati_type_dir)
                return priv->iconDirectory;
        }
    }

    return QVariant();
}

QModelIndex ActorVfsModel::index(int row, int column, const QModelIndex &parent) const
{
    const auto &entries = priv->entries;

    if (!parent.isValid())
    {
        int e = 0;

        for (int i = 0; i < row; i++)
        {
            int sibling = entries.at((size_t)e).sibling;
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
            e = entries[e].sibling;

    }

    return i;
}

int ActorVfsModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 3;

    if (!priv->entries.empty())
        return 3;

    return 0;
}

bool ActorVfsModel::canFetchMore(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return false;

    int e = parent.internalId();
    const Entry &entry = priv->entries[e];

    if (entry.fetchState != ENTRY_FETCH_STATE_IDLE)
    {
        return false;
    }

    if (entry.type == ati_type_dir)
    {
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

    if (entry.fetchState != ENTRY_FETCH_STATE_IDLE)
        return;

    if (entry.type == ati_type_dir)
    {
        entry.fetchState = ENTRY_FETCH_STATE_WAIT_START;

        DirFetcher fetch;

        fetch.entryIndex = e;
        fetch.timeout = 0;
        fetch.index = 0;

        for (;priv->entries[e].parent >= 0;)
        {
            e = priv->entries[e].parent;
        }

        fetch.actorId = priv->entries[e].value;
        priv->dirFetchers.push_back(fetch);
        listDirectoryRequest(priv->dirFetchers.back());
    }
}

QVariant ActorVfsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        if      (section == 0) return QLatin1String("Name");
        else if (section == 1) return QLatin1String("Type");
        else if (section == 2) return QLatin1String("Value");
    }

    return QVariant();
}
