typedef struct {
    char hardware_ID[16], maker_ID[16], device_info[16], country_codes[8];
    char ctrl[4], dev[1], VGA[1], WinCE[1], unk[1];
    char product_ID[10], product_version[6], release_date[16], boot_file[16];
    char software_maker_info[16], title[32];
} ipbin_meta_t;
