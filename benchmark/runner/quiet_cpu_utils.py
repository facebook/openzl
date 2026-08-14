# Copyright (c) Meta Platforms, Inc. and affiliates.

import asyncio
import logging
import re
import subprocess
import time
from typing import Dict, List, Optional, Tuple


class ReservedCpu:
    def __init__(self, mgr: "QuietCPUManager"):
        self.mgr = mgr
        self.cpu = None

    async def __aenter__(self):
        while True:
            async with self.mgr.lock:
                self.cpu = self.mgr.get_available_cpu()
                if self.cpu is not None:
                    return self
            await asyncio.sleep(0.01)

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        async with self.mgr.lock:
            self.mgr._release_cpu(self.cpu)


class QuietCPUManager:
    def __init__(self, verbose: bool = False):
        self._cpu_to_core = None
        self.num_cpus = self.get_num_cpus()
        self._used_cpus = set()
        self.lock = asyncio.Lock()
        self.cpus = set()
        self.isolated = False

        self._init_cpus_to_use()
        if verbose:
            logging.info(f"Using CPUs: {sorted(self.cpus)}")

    def _init_cpus_to_use(self):
        self.cpus = set(self.get_isolated_cpus())
        if self.cpus:
            self.isolated = True
        else:
            self.isolated = False
            self.cpus = set(
                self.get_quiet_cpus(int(len(self.get_cpu_schedule_info()) * 3 / 4))
            )
        self.cpus &= set(self.get_physical_cpus())
        self.cpus -= {0, max(self.get_physical_cpus())}

    def reserve(self) -> ReservedCpu:
        return ReservedCpu(self)

    def get_available_cpu(self) -> Optional[int]:
        available_cpus = self.cpus - self._used_cpus
        if len(available_cpus) == 0:
            return None
        if self.isolated:
            # We expect tickless only on isolated systems
            tickless_available = set(self.get_tickless_cpus()) & available_cpus
            if len(tickless_available) == 0:
                return None
            cpu = sorted(tickless_available)[0]
        else:
            # TODO: Can look for the quietiest cpu in the future
            cpu = available_cpus.pop()
        self._used_cpus.add(cpu)
        return cpu

    def _release_cpu(self, cpu: int):
        self._used_cpus = self._used_cpus - {cpu}

    def get_cpu_to_core(self) -> Dict[int, Tuple[int, int]]:
        if self._cpu_to_core is None:
            lscpu = subprocess.check_output(["lscpu", "-p"]).decode()
            cpu_core_socket = re.findall(r"^(\d+),(\d+),(\d+),.*$", lscpu, re.MULTILINE)
            cpu_to_core = {}
            for cpu, core, socket in cpu_core_socket:
                cpu_to_core[int(cpu)] = (int(core), int(socket))
            self._cpu_to_core = cpu_to_core
        return self._cpu_to_core

    def get_physical_cpus(self) -> List[int]:
        cpu_to_core = self.get_cpu_to_core()
        cores_seen = set()
        res = []
        for cpu in sorted(cpu_to_core.keys()):
            core = cpu_to_core[cpu]
            if core not in cores_seen:
                cores_seen.add(core)
                res.append(cpu)
        return res

    def get_num_cpus(self) -> int:
        return len(self.get_physical_cpus())

    def get_isolated_cpus(self) -> List[int]:
        with open("/sys/devices/system/cpu/isolated", "r") as isolated:
            lines = isolated.readlines()
        lines = lines[0].strip()
        if not lines:
            return []
        cpus = []
        groups = lines.split(",")
        for group in groups:
            if "-" in group:
                lower, upper = group.split("-")
                cpus += list(range(int(lower), int(upper) + 1))
            else:
                cpus.append(int(group))
        return cpus

    def _read_proc_interrupts(self):
        with open("/proc/interrupts", "r") as interrupts:
            data = interrupts.read()
        return [int(v) for v in re.findall(r"LOC:([\d\s]*)[^\d\s].*", data)[0].split()]

    def _read_ps_cpu_psr(self):
        ps = subprocess.check_output(["ps", "-emo", "%cpu,psr"])
        ps = ps.splitlines()[1:]
        cleaned = []
        for line in ps:
            cpu, psr = line.strip().split()
            if psr.isdigit():
                psr = int(psr)
            else:
                psr = None
            cleaned.append((float(cpu), psr))
        return cleaned

    def get_tickless_cpus(self, interval: int = 1, threshold: int = 10) -> List[int]:
        # TODO: Can also check for no activity on virtual sibling
        before = self._read_proc_interrupts()
        time.sleep(interval)
        after = self._read_proc_interrupts()
        tickless = {}
        for c in self.cpus:
            if after[c] - before[c] < threshold:
                tickless[c] = after[c] - before[c]
        return sorted(tickless.keys(), key=lambda x: (tickless[x], x))

    def get_cpu_schedule_info(self) -> Dict[int, float]:
        res = {}
        for ps in self._read_ps_cpu_psr():
            cpu = ps[1]
            if cpu is not None:
                if cpu not in res:
                    res[cpu] = 0.0
                res[cpu] += ps[0]
        return res

    def get_unscheduled_cpus(self, usage_threshold: float = 0.0) -> List[int]:
        schedule_info = self.get_cpu_schedule_info()
        cpus = []
        for cpu, usage in schedule_info.items():
            if usage <= usage_threshold:
                cpus.append(cpu)
        return cpus

    def get_quiet_cpus(self, num) -> List[int]:
        schedule_info = self.get_cpu_schedule_info()
        if len(schedule_info) < num:
            raise RuntimeError(
                f"`{num}` cpus requested, `{len(schedule_info)}` available"
            )
        infos = sorted((usage, cpu) for cpu, usage in schedule_info.items())
        infos = infos[:num]
        cpus = sorted(info[1] for info in infos)
        return cpus
