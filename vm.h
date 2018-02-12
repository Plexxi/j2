/*
 * j2 - A simple concatenative programming language that can understand
 *      the structure of and dynamically link with compatible C binaries.
 *
 * Copyright (C) 2011 Jason Nyberg <jasonnyberg@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef VM_H
#define VM_H

enum {
    VMOP_CODE_END,
    VMOP_EXT,
    VMOP_CTX_POP,
    VMOP_CTX_KEEP,
    VMOP_CATCH,
    VMOP_TERM_START,
    VMOP_BUILTIN, // start of non-excep
    VMOP_PUSHWIP,
    VMOP_REF,
    VMOP_DEREF,
    VMOP_ASSIGN,
    VMOP_REMOVE,
    VMOP_EVAL,
    VMOP_THROW,
    VMOP_COMPARE,
    VMOP_SPLIT,
    VMOP_MERGE,
    VMOP_RDLOCK,
    VMOP_WRLOCK,
    VMOP_UNLOCK,
    VMOP_FUN_START,
    VMOP_CTX_START,
    VMOP_BLK_START,
    VMOP_COUNT
} VM_OPCODES;

extern int vm_boot();

#endif // VM_H
