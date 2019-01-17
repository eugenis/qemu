/*
 *  Linux signal related syscalls
 *  Copyright (c) 2003 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifdef TARGET_NR_alarm
SYSCALL_IMPL(alarm)
{
    return alarm(arg1);
}
#endif

SYSCALL_IMPL(kill)
{
    return get_errno(safe_kill(arg1, target_to_host_signal(arg2)));
}

#ifdef TARGET_NR_pause
SYSCALL_IMPL(pause)
{
    if (!block_signals()) {
        CPUState *cpu = ENV_GET_CPU(cpu_env);
        TaskState *ts = cpu->opaque;
        sigsuspend(&ts->signal_mask);
    }
    return -TARGET_EINTR;
}
#endif
