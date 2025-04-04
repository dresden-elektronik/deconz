/*
 * Copyright (c) 2013-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <deconz/dbg_trace.h>
#include <deconz/u_assert.h>
#include "source_routing.h"
#include "zm_controller.h"
#include "zm_neighbor.h"
#include "zm_node.h"

#define MAX_TRASH_ROUTE_TTL 16
#define MAX_TRASH_ROUTES 16
#define MAX_ROUTE_ERRORS 6
#define MAX_RECV_ERRORS 3

static size_t MaxRecvErrors = 11;

struct TrashRoute
{
    uint16_t ttl;
    uint16_t n_hops;
    uint16_t hops[deCONZ::SourceRoute::MaxHops];
};

static int trashRouteInsertIter;
static TrashRoute trashRoutes[MAX_TRASH_ROUTES];

namespace deCONZ {

SourceRouting::SourceRouting()
{

}

static void addTrashRoute(const SourceRoute &sr)
{
    trashRouteInsertIter += 1;
    trashRouteInsertIter &= (MAX_TRASH_ROUTES - 1);

    TrashRoute *tr = &trashRoutes[trashRouteInsertIter];

    size_t i;
    for (i = 0; i < sr.hops().size(); i++)
    {
        tr->hops[i] = sr.hops()[i].nwk();
    }

    tr->ttl = MAX_TRASH_ROUTE_TTL;
    tr->n_hops = (uint16_t)sr.hops().size();
}

static TrashRoute *getTrashedRoute(const SourceRoute &sr)
{
    for (int i = 0; i < MAX_TRASH_ROUTES; i++)
    {
        TrashRoute *tr = &trashRoutes[i];

        if (tr->ttl && sr.hops().size() == tr->n_hops)
        {
            unsigned matches = 0;
            for (size_t j = 0; j < sr.hops().size(); j++)
            {
                if (tr->hops[j] != sr.hops()[j].nwk())
                    break;

                matches++;
            }

            if (matches == sr.hops().size())
                return tr;
        }
    }

    return nullptr;
}

/*! Returns true if a source route with given hops exists. */
static bool sourceRouteExists(const std::vector<deCONZ::Address> &hops, const std::vector<SourceRoute> &routes)
{
    for (const auto &route : routes)
    {
        if (route.hops().size() < hops.size())
        {
            continue;
        }

        size_t match = 0;
        for (size_t i = 0; i < hops.size(); i++)
        {
            if (hops.at(i).ext() == route.hops().at(route.hops().size() - hops.size() + i).ext())
            {
                match++;
            }
            else
            {
                break;
            }
        }

        if (match == hops.size())
        {
            return true;
        }
    }
    return false;
}

std::vector<SourceRoute> sourceRoutesForDestination(const deCONZ::Address &dst, const std::vector<SourceRoute> &routes)
{
    std::vector<SourceRoute> result;

    std::copy_if(routes.begin(), routes.end(), std::back_inserter(result), [&dst](const SourceRoute &route)
    {
        return route.state() != SourceRoute::StateSleep && !route.hops().empty() && dst.ext() == route.hops().back().ext();
    });

    return result;
}

static const NodeInfo *getNodeForAddress(const deCONZ::Address &addr, const std::vector<NodeInfo> &nodes)
{
    auto i = std::find_if(nodes.begin(), nodes.end(), [&addr](const NodeInfo &node)
    {
        return node.data && node.data->address().ext() == addr.ext();
    });

    return i != nodes.end() ? &*i : nullptr;
}

static bool updateSourceRoute(SourceRoute &route, const std::vector<NodeInfo> &nodes)
{
    int updates = 0;
    const NodeInfo *prevNode = nullptr;

    for (size_t i = 0; i < route.hops().size(); i++)
    {
        const auto hop = route.hops().at(i);
        const auto *node = getNodeForAddress(hop, nodes);

        if (hop.ext() == nodes.front().data->address().ext())
        {
            route.m_hopLqi[i] = 255; // coordinator
            prevNode = node;
            continue;
        }

        if (!node || !node->isValid())
        {
            if (route.m_hopLqi[i] != 0)
            {
                updates++;
                route.m_hopLqi[i] = 0; // chain broken, route isn't usable anymore
            }
            break;
        }

        if (node->data->isZombie() || node->data->recvErrors() >= MAX_RECV_ERRORS)
        {
            route.incrementErrors(); // slowly bring in error rate to accelerate route removal
        }

        if (prevNode)
        {
            const zmNeighbor *neib = prevNode->data->getNeighbor(hop);
            if (neib)
            {
                quint8 lqi = neib->lqi();

                if (route.m_hopLqi[i] != lqi)
                {
                    updates++;
                    route.m_hopLqi[i] = lqi;
                }
            }
        }

        prevNode = node;
    }

    return updates > 0;
}

static void selectBestSourceRouteForNode(const NodeInfo &node, std::vector<SourceRoute> &routes)
{
    auto dstRoutes = sourceRoutesForDestination(node.data->address(), routes);

    if (dstRoutes.empty())
    {
        return;
    }

    {
        const auto &sourceRoutes = node.data->sourceRoutes();
        if (!sourceRoutes.empty() && sourceRoutes.front().isOperational())
        {
            return;
        }
    }

    std::sort(dstRoutes.begin(), dstRoutes.end(), [](const SourceRoute &a, const SourceRoute &b)
    {
        bool result = false;
        const bool aOperational = a.isOperational();
        const bool bOperational = b.isOperational();

        if (a.txOk() > b.txOk())
        {
            result = true;
        }
        else if (!aOperational)
        {  }
        else if (a.errors() > b.errors() && bOperational)
        {  }
        else if (!bOperational)
        {
            result = true;
        }
        else if (aOperational && a.hops().size() < b.hops().size())
        {
            result = true;
        }

        return result;
    });

    const auto &route = dstRoutes.front();

    if (route.isOperational() && route.errors() < MaxRecvErrors)
    {
        while (1)
        {
            auto i = std::find_if(node.data->sourceRoutes().begin(), node.data->sourceRoutes().end(), [&route](const auto &sr)
            {
                return sr.uuidHash() != route.uuidHash();
            });

            if (i != node.data->sourceRoutes().end())
            {
                const auto uuid = i->uuid();
                node.data->removeSourceRoute(i->uuidHash());
                emit deCONZ::controller()->sourceRouteDeleted(uuid);
                continue;
            }

            break;
        }

        node.data->addSourceRoute(route);
        emit deCONZ::controller()->sourceRouteChanged(route);
    }
}


static void calculateRouteForNode(const NodeInfo &node, const std::vector<NodeInfo> &nodes, size_t routeIter, std::vector<SourceRoute> &routes, const int minLqi, const int maxHops, size_t tickCounter)
{
    if (!node.data)
    {
        return;
    }

    DBG_Assert(!nodes.empty());

    deCONZ::zmNode *node1 = node.data;

    if (!node1->isRouter())
    {
        return;
    }

    {
        const auto *coord = nodes.front().data;

        if (!coord->isCoordinator())
        {
            DBG_Printf(DBG_ROUTING, "Node[0] expected to be coordinator %s (due nwk: 0x%04X), routeIter: %u\n", coord->extAddressString().c_str(), coord->address().nwk(), (unsigned)routeIter);
            return;
        }

        if (coord->address().nwk() == node1->address().nwk())
        {
            DBG_Printf(DBG_ROUTING, "Ignore node as hop %s (due nwk: 0x%04X), routeIter: %u\n", node1->extAddressString().c_str(), node1->address().nwk(), (unsigned)routeIter);
            return;
        }
    }

    if (DBG_IsEnabled(DBG_INFO_L2))
    {
        DBG_Printf(DBG_ROUTING, "Calc source routes for %s, routeIter: %u\n", node1->extAddressString().c_str(), (unsigned)routeIter);
    }

    if (!node1->address().hasNwk())
    {
        DBG_Printf(DBG_ROUTING, "Ignore node as hop %s (no nwk address), routeIter: %u\n", node1->extAddressString().c_str(), (unsigned)routeIter);
        return;
    }

    U_ASSERT(routeIter < routes.size());
    auto &route = routes[routeIter];

    U_ASSERT(maxHops > 2);
    DBG_Assert(!route.hops().empty());
    if (route.hops().empty())
    {
        return;
    }

    if (route.hasHop(node1->address()))
    {
        route.updateHopAddress(node1->address());

        if (route.hops().back().ext() == node1->address().ext())
        {
            bool updated = updateSourceRoute(route, nodes);
            if (route.errors() >= MAX_ROUTE_ERRORS && route.txOk() < route.errors())
            {
                if (/*route.uuid().startsWith(QLatin1String("auto-")) &&*/ (tickCounter > (1000 / zmController::MainTickMs) * 60))
                {
                    const auto uuid = route.uuid(); // copy since route is reference
                    DBG_Printf(DBG_ROUTING, "Remove source route to " FMT_MAC ": uuid: %s\n", FMT_MAC_CAST(route.hops().back().ext()), qPrintable(route.uuid()));
                    if (!getTrashedRoute(route))
                    {
                        addTrashRoute(route);
                    }
                    node.data->removeSourceRoute(route.uuidHash());
                    routes.erase(routes.begin() + routeIter);
                    emit deCONZ::controller()->sourceRouteDeleted(uuid);
                }
            }
            else if (updated)
            {
                DBG_Printf(DBG_ROUTING, "Updated source route to " FMT_MAC "\n", FMT_MAC_CAST(route.hops().back().ext()));
                if (node.data->updateSourceRoute(route))
                {
                    emit deCONZ::controller()->sourceRouteChanged(route);
                }
            }
        }

        return;
    }

    if (route.state() == SourceRoute::StateSleep)
    {
        return;
    }

    if (route.hops().size() >= static_cast<size_t>(maxHops))
    {
        return;
    }

    if (route.hops().size() > 1 && (route.txOk() < 3 || route.errors() > route.txOk()))
    {
        return;
    }

    // require target node knows last hop
    // todo disabled because might be too restrive
    // auto *lastHopNeib = node1->getNeighbor(route.hops().back());
    // if (!lastHopNeib || lastHopNeib->lqi() < static_cast<uint8_t>(minLqi))
    // {
    //     return;
    // }

    auto *lastHopNode = getNodeForAddress(route.hops().back(), nodes);

    if (!lastHopNode || !lastHopNode->isValid() || lastHopNode->data->recvErrors() > 1 || lastHopNode->data->nodeDescriptor().isNull())
    {
        return;
    }

    // exclude old FLS firmware
    if (route.hops().size() > 1 && lastHopNode->data->nodeDescriptor().manufacturerCode() == 0x1135)
    {
        uint32_t version = lastHopNode->data->swVersionNum();
        if (version < 0x201000F1)
        {
            return;
        }
    }

    // has forward neighbor entry from last hop to node?
    const zmNeighbor *selfNeib = lastHopNode->data->getNeighbor(node.data->address());
    if (!selfNeib || selfNeib->lqi() < static_cast<uint8_t>(minLqi))
    {
        return;
    }

    auto hops = route.hops();
    hops.push_back(node1->address());

    const auto dstRoutes = sourceRoutesForDestination(node1->address(), routes);

    int betterCount = 0;
    for (const auto &route0 : dstRoutes)
    {
        if (route0.hops().size() <= hops.size() && route0.isOperational() && route0.errors() <= route.errors() && route0.txOk() > 0)
        {
            betterCount++;
        }
    }

    DBG_Assert(minLqi > 0);
    DBG_Assert(minLqi <= 255);

    if (selfNeib->lqi() < static_cast<uint8_t>(minLqi)) // TODO(mpi): already checked above
    {
        DBG_Printf(DBG_ROUTING, "Skip source routes via %s, low LQI: %u\n", lastHopNode->data->extAddressString().c_str(), selfNeib->lqi());
        return;
    }

    if (dstRoutes.size() >= 2)
    {
    }
    else if (betterCount >= 2)
    {
        DBG_Printf(DBG_ROUTING, "Skip source routes via %s, already enough routes\n", lastHopNode->data->extAddressString().c_str());
    }
    else if (route.isOperational() && !sourceRouteExists(hops, routes))
    {
        int order = dstRoutes.size() + 10;
        SourceRoute route1(createUuid(QLatin1String("auto-")), order,  route.hops()); // clone
        for (size_t i = 0; i < SourceRoute::MaxHops; i++)
        {
            route1.m_hopLqi[i] = route.m_hopLqi[i]; // TODO ctor
        }
        route1.addHop(node1->address(), selfNeib->lqi()); // extend
        TrashRoute *tr = getTrashedRoute(route1);
        if (tr)
        {
            if (tr->ttl > 0)
            {
                tr->ttl -= 1;
                if (tr->ttl == 0)
                {
                    tr->n_hops = 0;
                    DBG_Printf(DBG_ROUTING, "Remove route trash entry to %s via %s\n", node.data->extAddressString().c_str(), lastHopNode->data->extAddressString().c_str());
                }
            }

            if (tr->ttl > 0)
            {
                DBG_Printf(DBG_ROUTING, "Skip source routes via %s, has trash entry\n", lastHopNode->data->extAddressString().c_str());
            }
            return;
        }

        routes.push_back(route1);

        DBG_Printf(DBG_ROUTING, "Add auto source route to %s, last hop LQI: %u\n", node.data->extAddressString().c_str(), selfNeib->lqi());

        int i = 0;
        for (const auto &hop : route1.hops())
        {
            DBG_Printf(DBG_ROUTING, "  - Hop[%d] " FMT_MAC " (0x%04X), lqi: %u\n", i, FMT_MAC_CAST(hop.ext()), hop.nwk(), route1.m_hopLqi[i]);
            i++;
        }

        if (node1->sourceRoutes().empty())
        {
            node.data->addSourceRoute(route1);
            emit deCONZ::controller()->sourceRouteChanged(route1);
        }
    }
}

/*! Returns the source route for its uuid hash.
 */
SourceRoute *SR_GetRouteForUuidHash(std::vector<SourceRoute> &sourceRoutes, const uint uuid)
{
    auto i = std::find_if(sourceRoutes.begin(), sourceRoutes.end(), [&uuid](const SourceRoute &route)
    {
        return route.uuidHash() == uuid;
    });

    return i != sourceRoutes.end() ? &*i : nullptr;
}

void SR_CalculateRouteForNode(const std::vector<NodeInfo> &nodes, std::vector<deCONZ::SourceRoute> &routes, int minLqi, int maxHops, size_t tickCounter)
{
    static bool init = false;
    static size_t routeIter = 0;
    static size_t nodeIter = 0;

    auto coord = nodes.front();

    if (nodes.empty() || coord.data->neighbors().empty())
    {
        return;
    }

    // start with coordinator
    if (routes.empty() || !init)
    {
        init = true;
        routes.push_back(SourceRoute(createUuid(QLatin1String("auto-")), 0, {coord.data->address()}));
        routes.back().m_hopLqi[0] = 255;
    }

//    DBG_MEASURE_START(CORE_CalculateSourceRoutes);

    const auto oldRoutesSize = routes.size();

    const auto node = nodes[nodeIter % nodes.size()];

    if (!node.isValid() || node.data->isEndDevice())
    {
        // proceed with next
        routeIter = 0;
        nodeIter = (nodeIter + 1) % nodes.size();
        return;
    }

    {
        calculateRouteForNode(node, nodes, routeIter % routes.size(), routes, minLqi, maxHops, tickCounter);
        routeIter++;

        if (routes.size() != oldRoutesSize)
        {
            routeIter = routes.size(); // proceed with next node
        }

        if (routeIter >= routes.size())
        {
            selectBestSourceRouteForNode(node, routes);
        }
    }

    if (routeIter >= routes.size())
    {
        routeIter = 0;
        nodeIter = (nodeIter + 1) % nodes.size();
    }

    if (routes.size() > oldRoutesSize)
    {
        DBG_Printf(DBG_ROUTING, "Auto created source routes count: %u\n", (unsigned)routes.size());

        std::sort(routes.begin(), routes.end(), [](const SourceRoute &a, const SourceRoute &b)
        {
            DBG_Assert(!a.hops().empty());
            DBG_Assert(!b.hops().empty());

            return a.hops().back().ext() == b.hops().back().ext()
                   && a.hops().size() < b.hops().size();
        });
    }

//    DBG_MEASURE_END(CORE_CalculateSourceRoutes);
}

} // namespace deCONZ
