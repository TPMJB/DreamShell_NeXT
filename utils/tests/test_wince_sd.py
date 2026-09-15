"""Exercise production GD requests through the SD CPU-copy transfer paths."""
import unittest

from test_iso_loader_next import ROOT, compile_run


class WinCESDChecks(unittest.TestCase):
    def test_dma_physical_addresses_and_pio_virtual_addresses(self):
        source = (ROOT / "firmware/isoldr/loader/syscalls.c").read_text()
        request = source[source.index("static int is_transfer_cmd("):
                         source.index("/**\n * Main loop")]
        transfer = source[source.index("static void data_transfer_cb("):
                          source.index("static void data_transfer_dma_stream(")]
        # The extracted transfer section ends an outer _FS_ASYNC block.
        transfer = "#if _FS_ASYNC\n" + transfer
        header = (ROOT / "firmware/isoldr/loader/include/main.h").read_text()
        address_macros = "\n".join(line for line in header.splitlines()
                                   if line.startswith(("#define PHYS_ADDR(",
                                                       "#define CACHED_ADDR(")))
        support = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef uint8_t uint8;
typedef uintptr_t uint32;
typedef unsigned uint;
enum { BIN_TYPE_KATANA=1, BIN_TYPE_KOS=2, BIN_TYPE_WINCE=3 };
enum { CMD_PIOREAD=16, CMD_DMAREAD=17, CMD_DMAREAD_STREAM=28,
       CMD_DMAREAD_STREAM_EX=38, CMD_PIOREAD_STREAM=37,
       CMD_PIOREAD_STREAM_EX=39, CMD_MAX=47 };
enum { CMD_STAT_FAILED=-1, CMD_STAT_IDLE=0, CMD_STAT_PROCESSING=1,
       CMD_STAT_COMPLETED=2, CMD_WAIT_INTERNAL=0, CMD_WAIT_IRQ=1,
       CD_STATUS_PAUSED=1, CD_STATUS_PLAYING=2, GDC_CHN_ERROR=0,
       FS_DMA_DISABLED=0, FS_DMA_SHARED=1, FS_DMA_HIDDEN=2 };
#define LOGF(...) ((void)0)
#define LOGFF(...) ((void)0)
#define DBGFF(...) ((void)0)
typedef struct {
    int cmd,status,ata_status,cmd_abort,err,drv_stat,req_count,true_async;
    uint32 param[4],requested,transfered;
    struct { unsigned sec_size; } gdc;
} gd_state_t;
static gd_state_t state;
static struct { struct { int type; } exec; unsigned emu_async; int use_dma,alt_read; } info;
#define IsoInfo (&info)
static gd_state_t *get_GDS(void) { return &state; }
static int lock_gdsys(void) { return 0; }
static void unlock_gdsys(void) {}
static void OpenLog(void) {}
static int get_params_count(int cmd) { (void)cmd; return 4; }
static int iso_fd,read_count,purge_count,exits;
static uintptr_t read_addr[32],purge_addr[32];
static unsigned read_sectors[32],purge_bytes[32];
static void fs_enable_dma(int mode) { (void)mode; }
static int ReadSectors(uint8 *buf,int sector,int count,void (*cb)(size_t)) {
    assert(read_count < 32 && sector >= 45150 && count > 0);
    read_addr[read_count]=(uintptr_t)buf;read_sectors[read_count++]=count;
    return cb ? CMD_STAT_PROCESSING : CMD_STAT_COMPLETED;
}
static void dcache_purge_range(uintptr_t addr,unsigned bytes) {
    assert(purge_count < 32);purge_addr[purge_count]=addr;purge_bytes[purge_count++]=bytes;
}
static void gdcExitToGame(void) { ++exits; }
static int poll(int fd) { (void)fd;return 0; }
static int pre_read_xfer_done(void) { return 1; }
static void pre_read_xfer_end(void) {}
static void abort_data_cmd(void) { assert(0); }
static void g1_dma_set_irq_mask(int mask) { (void)mask; }
'''
        cases = r'''
static void run(int type,int cmd,uintptr_t supplied,uintptr_t expected,
                unsigned count,unsigned async,int true_async) {
    memset(&state,0,sizeof(state));memset(&info,0,sizeof(info));
    state.gdc.sec_size=2048;state.true_async=true_async;
    info.exec.type=type;info.emu_async=async;
    read_count=purge_count=exits=0;
    uint32 params[4]={45150,count,supplied,0};
    assert(gdcReqCmd(cmd,params)>0);
    assert(params[2]==supplied); /* Caller-owned command arguments stay intact. */
    assert(state.param[2]==expected && state.requested==count*2048);
    data_transfer();
    assert(state.status==CMD_STAT_COMPLETED && state.transfered==count*2048);
    assert(!state.requested && read_count>0);
    unsigned copied=0;
    for(int i=0;i<read_count;++i) {
        assert(read_addr[i]==expected+copied);
        copied+=read_sectors[i]*2048;
    }
    assert(copied==count*2048);
    if(cmd==CMD_DMAREAD
#ifdef DEV_TYPE_IDE
       && !true_async
#endif
    ) {
        assert(purge_count==read_count);
        for(int i=0;i<read_count;++i) {
            assert(purge_addr[i]==read_addr[i]);
            assert(purge_bytes[i]==read_sectors[i]*2048);
        }
    } else assert(!purge_count);
}
int main(void) {
#ifdef DEV_TYPE_SD
    const uintptr_t dma_expected=0x8c040000;
#else
    const uintptr_t dma_expected=0x0c040000;
#endif
    /* Synchronous, single-sector, chunked, large and pseudo-async reads. */
    run(BIN_TYPE_WINCE,CMD_DMAREAD,0x0c040000,dma_expected,3,0,0);
    run(BIN_TYPE_WINCE,CMD_DMAREAD,0x0c040000,dma_expected,1,8,0);
    run(BIN_TYPE_WINCE,CMD_DMAREAD,0x0c040000,dma_expected,18,8,0);
    assert(read_count==3 && read_sectors[0]==8 && read_sectors[2]==2);
    run(BIN_TYPE_WINCE,CMD_DMAREAD,0x0c040000,dma_expected,100,8,0);
    run(BIN_TYPE_WINCE,CMD_DMAREAD,0x0c040000,dma_expected,9,8,1);
    assert(read_count==1);
    /* A WinCE PIO pointer may refer to an unrelated virtual page. */
    run(BIN_TYPE_WINCE,CMD_PIOREAD,0x00400000,0x00400000,18,8,0);
    run(BIN_TYPE_WINCE,CMD_PIOREAD,0xc0040000,0xc0040000,3,0,0);
    /* Preserve non-WinCE behavior, including the working Katana game path. */
    run(BIN_TYPE_KATANA,CMD_DMAREAD,0x0c040000,0x0c040000,18,8,0);
    run(BIN_TYPE_KOS,CMD_PIOREAD,0x8c040000,0x8c040000,3,0,0);
#ifdef DEV_TYPE_SD
    run(BIN_TYPE_WINCE,CMD_DMAREAD,0x8c040000,0x8c040000,3,0,0);
    run(BIN_TYPE_WINCE,CMD_DMAREAD,0xac040000,0x8c040000,3,0,0);
#endif
    return 0;
}
'''
        for device in ("DEV_TYPE_SD", "DEV_TYPE_IDE"):
            with self.subTest(device=device):
                compile_run(address_macros + "\n" + support + request + transfer + cases,
                            ["-D_FS_ASYNC=1", "-D" + device,
                             "-Wno-unused-function"])


if __name__ == "__main__":
    unittest.main()
