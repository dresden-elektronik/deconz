/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef SOURCE_ROUTING_H
#define SOURCE_ROUTING_H

#include "deconz/node.h"

struct NodeInfo;
namespace deCONZ {

class SourceRouting
{
public:
    SourceRouting();
};

SourceRoute *SR_GetRouteForUuidHash(std::vector<SourceRoute> &sourceRoutes, const uint uuid);
void SR_CalculateRouteForNode(const std::vector<NodeInfo> &nodes, std::vector<SourceRoute> &routes, int minLqi, int maxHops, size_t tickCounter);

}

#endif // SOURCE_ROUTING_H
