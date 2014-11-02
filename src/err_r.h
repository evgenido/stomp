/*
 * Copyright 2014 Larry Wu<rulerson@gmail.com>
 *
 *      2014-10-14   created     thread-safe error string, need gcc support, please ref here<https://gcc.gnu.org/onlinedocs/gcc-3.3/gcc/Thread-Local.html>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef ERR_R_H
#define ERR_R_H

void errstr_set(const char *errstr);
const char *errstr_get();

#endif /* ERR_R_H */
