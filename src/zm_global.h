/*
 * Copyright (c) 2013-2024 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef ZM_GLOBAL_H
#define ZM_GLOBAL_H

/*
 *  TODO(mpi): Make this interface actor model based.
 *  It really is part of UI/MainWindow but also called from controller.
 */
namespace deCONZ
{
    void setDeviceState(State state);
}

#endif // ZM_GLOBAL_H
