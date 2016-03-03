#include "fs.h"
#include "draw.h"
#include "hid.h"
#include "platform.h"
#include "aes.h"
#include "sha.h"
#include "sdmmc.h"
#include "nand.h"

#define NAND_BUFFER ((u8*)0x21100000)
#define NAND_BUFFER_SIZE (0x100000) // must be multiple of 0x200

static u8 CtrNandCtr[16];
static u8 TwlNandCtr[16];

static u32 emunand_base_sector = 0x000000;


bool InitNandCrypto(void)
{
    // STEP #1: Get NAND CID, set up TWL/CTR counter
    u8 NandCid[16];
    u8 shasum[32];
    
    sdmmc_get_cid( 1, (uint32_t*) NandCid);
    sha_init(SHA256_MODE);
    sha_update(NandCid, 16);
    sha_get(shasum);
    memcpy(CtrNandCtr, shasum, 16);
    sha_init(SHA1_MODE);
    sha_update(NandCid, 16);
    sha_get(shasum);
    for(u32 i = 0; i < 16; i++) // little endian and reversed order
        TwlNandCtr[i] = shasum[15-i];
    
    // STEP #2: Calculate slot 0x3 key, set it up to slot 0x11
    
        
    return true;
}

void CryptNand(u8* buffer, u32 sector, u32 count, u32 keyslot)
{
    u32 mode = (sector >= (0x0B100000 / 0x200)) ? AES_CNT_CTRNAND_MODE : AES_CNT_TWLNAND_MODE;
    u8 ctr[16] __attribute__((aligned(32)));
    
    // copy NAND CTR and increment it
    memcpy(ctr, (sector >= (0x0B100000 / 0x200)) ? CtrNandCtr : TwlNandCtr, 16);
    add_ctr(ctr, sector * (0x200/0x10));
    
    // decrypt the data
    use_aeskey(keyslot);
    for (u32 s = 0; s < count; s++) {
        for (u32 b = 0x0; b < 0x200; b += 0x10, buffer += 0x10) {
            set_ctr(ctr);
            aes_decrypt((void*) buffer, (void*) buffer, 1, mode);
            add_ctr(ctr, 0x1);
        }
    }
}

int ReadNandSectors(u8* buffer, u32 sector, u32 count, u32 keyslot, bool read_emunand)
{
    if (read_emunand) {
        int errorcode = sdmmc_sdcard_readsectors(emunand_base_sector + sector, count, buffer);
        if (errorcode) return errorcode;
    } else {
        int errorcode = sdmmc_nand_readsectors(sector, count, buffer);
        if (errorcode) return errorcode;   
    }
    CryptNand(buffer, sector, count, keyslot);
    
    return 0;
}

int WriteNandSectors(const u8* buffer, u32 sector, u32 count, u32 keyslot, bool write_emunand)
{
    // buffer must not be changed, so this is a little complicated
    for (u32 s = 0; s < count; s += (NAND_BUFFER_SIZE / 0x200)) {
        u32 pcount = min((NAND_BUFFER_SIZE/0x200), (count - s));
        memcpy(NAND_BUFFER, buffer + (s*0x200), pcount * 0x200);
        CryptNand(NAND_BUFFER, sector + s, pcount, keyslot);
        if (write_emunand) {
            int errorcode = sdmmc_sdcard_writesectors(emunand_base_sector + sector + s, pcount, NAND_BUFFER);
            if (errorcode) return errorcode;
        } else {
            int errorcode = sdmmc_nand_writesectors(sector + s, pcount, NAND_BUFFER);
            if (errorcode) return errorcode;   
        }
    }
    
    return 0;
}

u32 GetEmuNandBase(void)
{
    return emunand_base_sector;
}

u32 SwitchEmuNandBase(int start_sector)
{
    // switching code goes here
    return emunand_base_sector;
}

