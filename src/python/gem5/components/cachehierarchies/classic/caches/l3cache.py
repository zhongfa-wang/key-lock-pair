# [klp] L3 cache class

from typing import Type

from m5.objects import (
    BasePrefetcher,
    Cache,
    Clusivity,
    StridePrefetcher,
)

from .....utils.override import *


class L3Cache(Cache):
    """
    A simple L3 Cache with default values.
    """

    def __init__(
        self,
        size: str = "2MiB",
        assoc: int = 16,
        tag_latency: int = 20,
        data_latency: int = 20,
        response_latency: int = 1,
        mshrs: int = 64,
        tgts_per_mshr: int = 12,
        writeback_clean: bool = False,
        clusivity: Clusivity = "mostly_incl",
        PrefetcherCls = StridePrefetcher,
        # [klp] Adding CacheLevel parameter
        cache_level = 'L3'
    ):
        super().__init__()
        self.size = size
        self.assoc = assoc
        self.tag_latency = tag_latency
        self.data_latency = data_latency
        self.response_latency = response_latency
        self.mshrs = mshrs
        self.tgts_per_mshr = tgts_per_mshr
        self.writeback_clean = writeback_clean
        self.clusivity = clusivity
        self.prefetcher = PrefetcherCls()
        # [klp] Adding CacheLevel parameter
        self.cache_level = cache_level
