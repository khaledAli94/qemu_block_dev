This project implements a minimal FAT16/32 reader in a bare‑metal environment.
It accesses an SD card at the raw sector level, parses the MBR and FAT boot sector, 
walks directory entries, and follows FAT cluster chains to read file contents.
The project serves as an educational demonstration of how FAT16/32 organizes data on disk and how low‑level storage access works.
