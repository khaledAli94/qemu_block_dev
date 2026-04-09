This project implements a minimal FAT32 reader in a bare‑metal environment.
It accesses an SD card at the raw sector level, parses the MBR and FAT32 boot sector, 
walks directory entries, and follows FAT cluster chains to read file contents.
All parsing is performed manually without an operating system or filesystem libraries. 
The project serves as an educational demonstration of how FAT32 organizes data on disk and how low‑level storage access works.