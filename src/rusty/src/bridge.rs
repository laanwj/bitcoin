use std::ffi::c_void;
extern "C" {
    pub fn rusty_IsInitialBlockDownload() -> bool;
    pub fn rusty_ShutdownRequested() -> bool;

    fn rusty_ProcessNewBlock(blockdata: *const u8, blockdatalen: usize, blockindex_requested: *const c_void);

    /// Connects count headers serialized in a block of memory, each stride bytes from each other.
    /// Returns the last header which was connected, if any (or NULL).
    fn rusty_ConnectHeaders(headers: *const u8, stride: usize, count: usize) -> *const c_void;

    // Utilities to work with CBlockIndex pointers. Wrapped in a safe wrapper below.

    /// Gets a CBlockIndex* pointer (casted to a c_void) representing the current tip.
    /// Guaranteed to never be NULL (but may be genesis)
    fn rusty_GetChainTip() -> *const c_void;

    /// Gets a CBlockIndex* pointer (casted to a c_void) representing the genesis block.
    /// Guaranteed to never be NULL
    fn rusty_GetGenesisIndex() -> *const c_void;

    #[allow(dead_code)]
    /// Finds a CBlockIndex* for a given block hash, or NULL if none is found
    fn rusty_HashToIndex(hash: *const u8) -> *const c_void;

    #[allow(dead_code)]
    /// Gets the height of a given CBlockIndex* pointer
    fn rusty_IndexToHeight(index: *const c_void) -> i32;

    /// Gets the hash of a given CBlockIndex* pointer
    fn rusty_IndexToHash(index: *const c_void) -> *const u8;

    // Utilities for logging, wrapped in a macro below.

    #[allow(dead_code)]
    /// Return whether a given category should be logged
    pub fn rusty_LogAcceptCategory(cat: u32) -> bool;

    #[allow(dead_code)]
    /// Log a message (unconditionally)
    pub fn rusty_LogPrintStr(msg: *const u8, len: usize);
}

/// Log a message to the debug log (unconditionally)
#[allow(unused_macros)]
macro_rules! log {
    ($($arg:tt)+) => ({
        use crate::bridge::rusty_LogPrintStr;
        let msg = format!($($arg)+);
        unsafe { rusty_LogPrintStr(msg.as_ptr(), msg.len()); }
    })
}

/// Categories for logging using log_cat!
/// These must match `LogFlags` in `logging.h`.
#[allow(dead_code)]
#[repr(u32)]
pub enum LogFlags {
    NET         = (1 <<  0),
    TOR         = (1 <<  1),
    MEMPOOL     = (1 <<  2),
    HTTP        = (1 <<  3),
    BENCH       = (1 <<  4),
    ZMQ         = (1 <<  5),
    DB          = (1 <<  6),
    RPC         = (1 <<  7),
    ESTIMATEFEE = (1 <<  8),
    ADDRMAN     = (1 <<  9),
    SELECTCOINS = (1 << 10),
    REINDEX     = (1 << 11),
    CMPCTBLOCK  = (1 << 12),
    RAND        = (1 << 13),
    PRUNE       = (1 << 14),
    PROXY       = (1 << 15),
    MEMPOOLREJ  = (1 << 16),
    LIBEVENT    = (1 << 17),
    COINDB      = (1 << 18),
    QT          = (1 << 19),
    LEVELDB     = (1 << 20),
}

/// Log a message to the debug log (with category)
#[allow(unused_macros)]
macro_rules! log_cat {
    ($cat:expr, $($arg:tt)+) => ({
        use crate::bridge::{rusty_LogPrintStr,rusty_LogAcceptCategory};
        let cat = $cat;
        if unsafe { rusty_LogAcceptCategory(cat as u32) } {
            let msg = format!($($arg)+);
            unsafe { rusty_LogPrintStr(msg.as_ptr(), msg.len()); }
        }
    })
}

#[allow(dead_code)]
/// Connects the given array of (sorted, in chain order) headers (in serialized, 80-byte form).
/// Returns the last header which was connected, if any.
pub fn connect_headers(headers: &[[u8; 80]]) -> Option<BlockIndex> {
    if headers.is_empty() { return None; }
    let first_header = headers[0].as_ptr();
    let index = if headers.len() == 1 {
        unsafe { rusty_ConnectHeaders(first_header, 80, 1) }
    } else {
        let second_header = headers[1].as_ptr();
        let stride = second_header as usize - first_header as usize;
        unsafe { rusty_ConnectHeaders(first_header, stride, headers.len()) }
    };
    if index.is_null() { None } else { Some(BlockIndex { index }) }
}

/// Connects the given array of (sorted, in chain order) headers (in serialized, 80-byte form).
/// Returns the last header which was connected, if any.
pub fn connect_headers_flat_bytes(headers: &[u8]) -> Option<BlockIndex> {
    if headers.len() % 80 != 0 { return None; }
    if headers.is_empty() { return None; }
    let index = unsafe { rusty_ConnectHeaders(headers.as_ptr(), 80, headers.len() / 80) };
    if index.is_null() { None } else { Some(BlockIndex { index }) }
}

/// Processes a new block, in serialized form.
/// blockindex_requested_by_state shouild be set *only* if the given BlockIndex was provided by
/// BlockProviderState::get_next_block_to_download(), and may be set to None always.
pub fn connect_block(blockdata: &[u8], blockindex_requested_by_state: Option<BlockIndex>) {
    let blockindex = match blockindex_requested_by_state { Some(index) => index.index, None => std::ptr::null(), };
    unsafe {
        rusty_ProcessNewBlock(blockdata.as_ptr(), blockdata.len(), blockindex);
    }
}

#[derive(PartialEq, Clone, Copy)]
pub struct BlockIndex {
    index: *const c_void,
}

impl BlockIndex {
    pub fn tip() -> Self {
        Self {
            index: unsafe { rusty_GetChainTip() },
        }
    }

    #[allow(dead_code)]
    pub fn get_from_hash(hash: &[u8; 32]) -> Option<Self> {
        let index = unsafe { rusty_HashToIndex(hash.as_ptr()) };
        if index.is_null() {
            None
        } else {
            Some(Self { index })
        }
    }

    pub fn genesis() -> Self {
        Self {
            index: unsafe { rusty_GetGenesisIndex() },
        }
    }

    #[allow(dead_code)]
    pub fn height(&self) -> i32 {
        unsafe { rusty_IndexToHeight(self.index) }
    }

    pub fn hash(&self) -> [u8; 32] {
        let hashptr = unsafe { rusty_IndexToHash(self.index) };
        let mut res = [0u8; 32];
        unsafe { std::ptr::copy(hashptr, res.as_mut_ptr(), 32) };
        res
    }

    /// Gets the hex formatted hash of this block, in byte-revered order (ie starting with the PoW
    /// 0s, as is commonly used in Bitcoin APIs).
    pub fn hash_hex(&self) -> String {
        let hash_bytes = self.hash();
        let mut res = String::with_capacity(64);
        for b in hash_bytes.iter().rev() {
            res.push(std::char::from_digit((b >> 4) as u32, 16).unwrap());
            res.push(std::char::from_digit((b & 0x0f) as u32, 16).unwrap());
        }
        res
    }
}

extern "C" {
    // Utilities to work with BlockProviderState objects. Wrapped in a safe wrapper below.

    /// Creates a new BlockProviderState with a given current best CBlockIndex*.
    /// Don't forget to de-allocate!
    fn rusty_ProviderStateInit(blockindex: *const c_void) -> *mut c_void;
    /// De-allocates a BlockProviderState.
    fn rusty_ProviderStateFree(provider_state: *mut c_void);

    /// Sets the current best available CBlockIndex* for the given provider state.
    fn rusty_ProviderStateSetBest(provider_state: *mut c_void, blockindex: *const c_void);

    /// Gets the next CBlockIndex* a given provider should download, or NULL
    fn rusty_ProviderStateGetNextDownloads(providerindexvoid: *mut c_void, has_witness: bool) -> *const c_void;
}

pub struct BlockProviderState {
    // TODO: We should be smarter than to keep a copy of the current best pointer twice, but
    // crossing the FFI boundary just to look it up again sucks.
    current_best: BlockIndex,
    state: *mut c_void,
}
impl BlockProviderState {
    /// Initializes block provider state with a given current best header.
    /// Note that you can use a guess on the current best that moves backwards as you discover the
    /// providers' true chain state, though for efficiency you should try to avoid calling
    /// get_next_block_to_download in such a state.
    pub fn new_with_current_best(blockindex: BlockIndex) -> Self {
        Self {
            current_best: blockindex,
            state: unsafe { rusty_ProviderStateInit(blockindex.index) }
        }
    }

    /// Sets the current best available blockindex to the given one on this state.
    pub fn set_current_best(&mut self, blockindex: BlockIndex) {
        self.current_best = blockindex;
        unsafe { rusty_ProviderStateSetBest(self.state, blockindex.index) };
    }

    /// Gets the current best available blockindex as provided previously by set_current_best or
    /// new_with_current_best.
    pub fn get_current_best(&self) -> BlockIndex {
        self.current_best
    }

    /// Gets the BlockIndex representing the next block which should be downloaded, if any.
    pub fn get_next_block_to_download(&mut self, has_witness: bool) -> Option<BlockIndex> {
        let index = unsafe { rusty_ProviderStateGetNextDownloads(self.state, has_witness) };
        if index.is_null() { None } else { Some(BlockIndex { index }) }
    }
}
impl Drop for BlockProviderState {
    fn drop(&mut self) {
        unsafe { rusty_ProviderStateFree(self.state) };
    }
}
