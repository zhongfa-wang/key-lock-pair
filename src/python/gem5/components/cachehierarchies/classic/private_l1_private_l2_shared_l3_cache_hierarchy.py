# [klp] A private L1, private L2, Shared L3 cache

from typing import Optional

from m5.objects import (
    BadAddr,
    BaseCPU,
    BaseXBar,
    Cache,
    L2XBar,
    Port,
    SystemXBar,
)
from m5.params import *

from ....isas import ISA
from ....utils.override import *
from ...boards.abstract_board import AbstractBoard
from ..abstract_cache_hierarchy import AbstractCacheHierarchy
from ..abstract_three_level_cache_hierarchy import (
    AbstractThreeLevelCacheHierarchy,
)
from .abstract_classic_cache_hierarchy import AbstractClassicCacheHierarchy
from .caches.l1dcache import *
from .caches.l1icache import *
from .caches.l2cache import *
from .caches.l3cache import *
from .caches.mmu_cache import *


class PrivateL1PrivateL2SharedL3CacheHierarchy(
    AbstractClassicCacheHierarchy, AbstractThreeLevelCacheHierarchy
):
    """
    A cache setup where each core has a private L1 Data and Instruction Cache,
    private L2 cache, and a L3 cache is shared with all cores. The shared L3 cache is mostly
    inclusive with respect to the split I/D L1 and MMU caches.
    """

    def _get_default_membus(self) -> SystemXBar:
        """
        A method used to obtain the default memory bus of 64 bit in width for
        the PrivateL1PrivateL2SharedL3 CacheHierarchy.

        :returns: The default memory bus for the PrivateL1PrivateL2SharedL3
                  CacheHierarchy.

        :rtype: SystemXBar
        """
        membus = SystemXBar(width=64)
        membus.badaddr_responder = BadAddr()
        membus.default = membus.badaddr_responder.pio
        return membus

    def __init__(
        self,
        l1d_size: str,
        l1i_size: str,
        l2_size: str,
        l3_size: str,
        l1d_assoc: int = 8,
        l1i_assoc: int = 8,
        l2_assoc: int = 16,
        l3_assoc: int = 16,
        membus: Optional[BaseXBar] = None,
    ) -> None:

        AbstractClassicCacheHierarchy.__init__(self=self)
        AbstractThreeLevelCacheHierarchy.__init__(
            self,
            l1i_size=l1i_size,
            l1i_assoc=l1i_assoc,
            l1d_size=l1d_size,
            l1d_assoc=l1d_assoc,
            l2_size=l2_size,
            l2_assoc=l2_assoc,
            l3_size=l3_size,
            l3_assoc=l3_assoc,
        )

        self.membus = membus if membus else self._get_default_membus()

    @overrides(AbstractClassicCacheHierarchy)
    def get_mem_side_port(self) -> Port:
        return self.membus.mem_side_ports

    @overrides(AbstractClassicCacheHierarchy)
    def get_cpu_side_port(self) -> Port:
        return self.membus.cpu_side_ports

    @overrides(AbstractCacheHierarchy)
    def incorporate_cache(self, board: AbstractBoard) -> None:
        # Set up the system port for functional access from the simulator.
        board.connect_system_port(self.membus.cpu_side_ports)

        for _, port in board.get_mem_ports():
            self.membus.mem_side_ports = port

        self.l1icaches = [
            L1ICache(
                size=self._l1i_size,
                assoc=self._l1i_assoc,
                writeback_clean=False,
            )
            for i in range(board.get_processor().get_num_cores())
        ]
        self.l1dcaches = [
            L1DCache(size=self._l1d_size, assoc=self._l1d_assoc)
            for i in range(board.get_processor().get_num_cores())
        ]
        self.l2caches = [
            L2Cache(size=self._l2_size, assoc=self._l2_assoc)
            for i in range(board.get_processor().get_num_cores())
        ]
        self.l2buses = [
            L2XBar() for i in range(board.get_processor().get_num_cores())
        ]
        # self.l2cache = L2Cache(size=self._l2_size, assoc=self._l2_assoc)
        self.l3bus = L2XBar()
        self.l3cache = L3Cache(size=self._l3_size, assoc=self._l3_assoc)
        # ITLB Page walk caches
        self.iptw_caches = [
            MMUCache(size="8KiB", writeback_clean=False)
            for _ in range(board.get_processor().get_num_cores())
        ]
        # DTLB Page walk caches
        self.dptw_caches = [
            MMUCache(size="8KiB", writeback_clean=False)
            for _ in range(board.get_processor().get_num_cores())
        ]

        if board.has_coherent_io():
            self._setup_io_cache(board)

        for i, cpu in enumerate(board.get_processor().get_cores()):
            cpu.connect_icache(self.l1icaches[i].cpu_side)
            cpu.connect_dcache(self.l1dcaches[i].cpu_side)

            self.l1icaches[i].mem_side = self.l2buses[i].cpu_side_ports
            self.l1dcaches[i].mem_side = self.l2buses[i].cpu_side_ports

            self.l2caches[i].cpu_side = self.l2buses[i].mem_side_ports
            self.l2caches[i].mem_side = self.l3bus.cpu_side_ports

            self.iptw_caches[i].mem_side = self.l2buses[i].cpu_side_ports
            self.dptw_caches[i].mem_side = self.l2buses[i].cpu_side_ports

            cpu.connect_walker_ports(
                self.iptw_caches[i].cpu_side, self.dptw_caches[i].cpu_side
            )

            if board.get_processor().get_isa() == ISA.X86:
                int_req_port = self.membus.mem_side_ports
                int_resp_port = self.membus.cpu_side_ports
                cpu.connect_interrupt(int_req_port, int_resp_port)
            else:
                cpu.connect_interrupt()

        self.l3cache.cpu_side = self.l3bus.mem_side_ports
        self.l3cache.mem_side = self.membus.cpu_side_ports

    def _setup_io_cache(self, board: AbstractBoard) -> None:
        """Create a cache for coherent I/O connections"""
        self.iocache = Cache(
            assoc=8,
            tag_latency=50,
            data_latency=50,
            response_latency=50,
            mshrs=20,
            size="1KiB",
            tgts_per_mshr=12,
            addr_ranges=board.mem_ranges,
        )
        self.iocache.mem_side = self.membus.cpu_side_ports
        self.iocache.cpu_side = board.get_mem_side_coherent_io_port()


# [klp] {
class KLPPL1PL2SL3CacheHierarchy(PrivateL1PrivateL2SharedL3CacheHierarchy):
    """
    A cache setup where each core has a private L1 Data and Instruction Cache,
    private L2 cache, and a L3 cache is shared with all cores. The shared L3 cache is mostly
    inclusive with respect to the split I/D L1 and MMU caches.
    KLP configurations are passed to each cache levels
    """

    def __init__(
        self,
        tag_width: int,
        tag_pos: int,
        tag_granularity: int,
        threat_model: str,
        **kwargs
    ) -> None:

        PrivateL1PrivateL2SharedL3CacheHierarchy.__init__(self, **kwargs)
        self._tag_width = tag_width
        self._tag_pos = tag_pos
        self._tag_granularity = tag_granularity
        self._threat_model = threat_model

    @overrides(AbstractCacheHierarchy)
    def incorporate_cache(self, board: AbstractBoard) -> None:
        # Set up the system port for functional access from the simulator.
        board.connect_system_port(self.membus.cpu_side_ports)

        for _, port in board.get_mem_ports():
            self.membus.mem_side_ports = port

        self.l1icaches = [
            KLPL1ICache(
                tag_width=self._tag_width,
                tag_pos=self._tag_pos,
                tag_granularity=self._tag_granularity,
                threat_model=self._threat_model,
                size=self._l1i_size,
                assoc=self._l1i_assoc,
                writeback_clean=False,
            )
            for i in range(board.get_processor().get_num_cores())
        ]
        self.l1dcaches = [
            KLPL1DCache(
                tag_width=self._tag_width,
                tag_pos=self._tag_pos,
                tag_granularity=self._tag_granularity,
                threat_model=self._threat_model,
                size=self._l1d_size,
                assoc=self._l1d_assoc,
            )
            for i in range(board.get_processor().get_num_cores())
        ]
        self.l2caches = [
            KLPL2Cache(
                tag_width=self._tag_width,
                tag_pos=self._tag_pos,
                tag_granularity=self._tag_granularity,
                threat_model=self._threat_model,
                size=self._l2_size,
                assoc=self._l2_assoc,
            )
            for i in range(board.get_processor().get_num_cores())
        ]
        self.l2buses = [
            L2XBar() for i in range(board.get_processor().get_num_cores())
        ]

        self.l3bus = L2XBar()
        self.l3cache = KLPL3Cache(
            tag_width=self._tag_width,
            tag_pos=self._tag_pos,
            tag_granularity=self._tag_granularity,
            threat_model=self._threat_model,
            size=self._l3_size,
            assoc=self._l3_assoc,
        )
        # ITLB Page walk caches
        self.iptw_caches = [
            KLPMMUCache(
                tag_width=self._tag_width,
                tag_pos=self._tag_pos,
                tag_granularity=self._tag_granularity,
                threat_model=self._threat_model,
                size="8KiB",
                writeback_clean=False,
            )
            for _ in range(board.get_processor().get_num_cores())
        ]
        # DTLB Page walk caches
        self.dptw_caches = [
            KLPMMUCache(
                tag_width=self._tag_width,
                tag_pos=self._tag_pos,
                tag_granularity=self._tag_granularity,
                threat_model=self._threat_model,
                size="8KiB",
                writeback_clean=False,
            )
            for _ in range(board.get_processor().get_num_cores())
        ]

        if board.has_coherent_io():
            self._setup_io_cache(board)

        for i, cpu in enumerate(board.get_processor().get_cores()):
            cpu.connect_icache(self.l1icaches[i].cpu_side)
            cpu.connect_dcache(self.l1dcaches[i].cpu_side)

            self.l1icaches[i].mem_side = self.l2buses[i].cpu_side_ports
            self.l1dcaches[i].mem_side = self.l2buses[i].cpu_side_ports

            self.l2caches[i].cpu_side = self.l2buses[i].mem_side_ports
            self.l2caches[i].mem_side = self.l3bus.cpu_side_ports

            self.iptw_caches[i].mem_side = self.l2buses[i].cpu_side_ports
            self.dptw_caches[i].mem_side = self.l2buses[i].cpu_side_ports

            cpu.connect_walker_ports(
                self.iptw_caches[i].cpu_side, self.dptw_caches[i].cpu_side
            )

            if board.get_processor().get_isa() == ISA.X86:
                int_req_port = self.membus.mem_side_ports
                int_resp_port = self.membus.cpu_side_ports
                cpu.connect_interrupt(int_req_port, int_resp_port)
            else:
                cpu.connect_interrupt()

        self.l3cache.cpu_side = self.l3bus.mem_side_ports
        self.l3cache.mem_side = self.membus.cpu_side_ports


# } [klp]


class NoPrefetchKLPP1P2S3CacheHierarchy(KLPPL1PL2SL3CacheHierarchy):
    def incorporate_cache(self, board):
        super().incorporate_cache(board)
        if hasattr(self, "l1dcaches"):
            for cache in self.l1dcaches:
                cache.prefetcher = NULL
        if hasattr(self, "l2caches"):
            for cache in self.l2caches:
                cache.prefetcher = NULL
        if hasattr(self, "l3cache"):
            self.l3cache.prefetcher = NULL


class NoPrefetchP1P2S3CacheHierarchy(PrivateL1PrivateL2SharedL3CacheHierarchy):
    def incorporate_cache(self, board):
        super().incorporate_cache(board)
        if hasattr(self, "l1dcaches"):
            for cache in self.l1dcaches:
                cache.prefetcher = NULL
        if hasattr(self, "l2caches"):
            for cache in self.l2caches:
                cache.prefetcher = NULL
        if hasattr(self, "l3cache"):
            self.l3cache.prefetcher = NULL
