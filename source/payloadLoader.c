#include <gccore.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ogcsys.h>
#include <time.h>

#define PAYLOAD_FILE_NAME "AGDQPayload.dat"

static void *xfb = NULL;

static u8 SysArea[CARD_WORKAREA] ATTRIBUTE_ALIGN(32);

u32 first_frame = 1;
GXRModeObj *rmode = NULL;
void (*PSOreload)() = (void(*)())0x80001800;
static u32 safeBufferLocationTrustMe = 0x80EDA000; // Max value: 0x80EDA000
static u32 safeCodeLocationTrustMe = 0x80BDA000;


/*---------------------------------------------------------------------------------
	This function is called if a card is physically removed
---------------------------------------------------------------------------------*/
void card_removed(s32 chn,s32 result) {
//---------------------------------------------------------------------------------
	printf("card was removed from slot %c\n",(chn==0)?'A':'B');
	CARD_Unmount(chn);
}

//---------------------------------------------------------------------------------
int main() {
//---------------------------------------------------------------------------------
	VIDEO_Init();
	
	rmode = VIDEO_GetPreferredMode(NULL);

	PAD_Init();
	
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
		
	VIDEO_Configure(rmode);
		
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
	console_init(xfb,20,64,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*2);
	VIDEO_SetNextFramebuffer(xfb);

	printf("Memory Card Demo\n\n");

	while (1) {
		printf("Insert A card in slot B and press A\n");

		do {
			PAD_ScanPads();
			if (PAD_ButtonsDown(0) & PAD_BUTTON_START) PSOreload();
			VIDEO_WaitVSync();
		} while ( !(PAD_ButtonsDown(0) & PAD_BUTTON_A));


		printf("Mounting card ...\n");

		CARD_Init("GMBE","8P");
		int SlotB_error = CARD_Mount(CARD_SLOTA, SysArea, card_removed);
	
		printf("slot B code %d\n",SlotB_error);

		int CardError = 0;
		
		if (SlotB_error >= 0) {

			unsigned int SectorSize = 0;
			CARD_GetSectorSize(CARD_SLOTA,&SectorSize);

			printf("Sector size is %d bytes.\n\n",SectorSize);

			char *CardBuffer = (char *)memalign(32,SectorSize);
			
			
			printf("Starting directory\n");

			card_dir CardDir = {0};
			card_file CardFile = { 0 };
			
			CardError = CARD_FindFirst(CARD_SLOTA, &CardDir, true);

			bool found = false;
			
			while ( CARD_ERROR_NOFILE != CardError ) {
				printf("%s  %s  %s\n",CardDir.filename,CardDir.gamecode,CardDir.company);
				CardError = CARD_FindNext(&CardDir);
				if ( 0 == strcmp (PAYLOAD_FILE_NAME, (char *)CardDir.filename)) found = true; 
			};

			printf("Finished directory\n\n");
			
			if (found) {
				printf("Card Buffer location: %08lX\n", (u32)CardBuffer);
				printf("Sector Size: %d\n", SectorSize);
				printf("Test file contains :- \n");
				CardError = CARD_Open(CARD_SLOTA, PAYLOAD_FILE_NAME, &CardFile);
				
				if (!CardError) {
					u32 curLocation = 0x2000;

					// First read is an edge case (data starts at 0x50)
					CARD_Read(&CardFile, CardBuffer, SectorSize, curLocation);
					struct dol_s {
						unsigned long sec_pos[18];
						unsigned long sec_address[18];
						unsigned long sec_size[18];
						unsigned long bss_address, bss_size, entry_point;
					} dol = *((struct dol_s*)(CardBuffer + 0x50));
					for (u32 i = 0; i < 18; i++) {
						printf("Starting Section: %lu\n", i);
						
						u32 position = dol.sec_pos[i];
						u32 address = dol.sec_address[i] & 0xFFFFFF;
						u32 size = dol.sec_size[i];
						if (position == 0 || size == 0)
							continue;
						u32 sectorStart = (position + 0x50) / SectorSize;
						u32 sectorEnd = (position + size + 0x50) / SectorSize;
						u32 bytesLeft = size;
						u32 bytesWritten = 0;
						printf("Size: %lu\n", size);
						printf("DOL Address: %08lX\n", address);
						printf("Logical Address: %08lX\n", address + safeCodeLocationTrustMe);
						printf("Logical End Address: %08lX\n", address + safeCodeLocationTrustMe + size);
						do {
							PAD_ScanPads();
							if (PAD_ButtonsDown(0) & PAD_BUTTON_START) PSOreload();
							VIDEO_WaitVSync();
						} while (!(PAD_ButtonsDown(0) & PAD_BUTTON_A));
						for (u32 sector = sectorStart; sector <= sectorEnd; sector++) {
							
							CARD_Read(&CardFile, CardBuffer, SectorSize, sector * SectorSize);
							
							u32 amountToRead = SectorSize;
							if (bytesLeft < SectorSize) {
								amountToRead = bytesLeft;
							}
							if (sector == 0) {
								amountToRead -= 0x50;
								memcpy((void *)(safeCodeLocationTrustMe + address + bytesWritten), CardBuffer + 0x50, amountToRead);
							}
							else {
								memcpy((void *)(safeCodeLocationTrustMe + address + bytesWritten), CardBuffer, amountToRead);
							}
							bytesLeft -= amountToRead;
							bytesWritten += amountToRead;
						}

						printf("Finished Section: %lu\n", i);
					}
					CARD_Close(&CardFile);
					CARD_Unmount(CARD_SLOTA);
					free(CardBuffer);


					u32 entryPoint = dol.entry_point;
					entryPoint &= 0xFFFFFF;
					entryPoint += safeCodeLocationTrustMe;
					void(*entrypointFunc)() = (void(*)())(entryPoint);

					printf("Entry Point: %08lX\n", entryPoint);
					printf("Press A to jump\n");

					do {
						PAD_ScanPads();
						if (PAD_ButtonsDown(0) & PAD_BUTTON_START) PSOreload();
						VIDEO_WaitVSync();
					} while (!(PAD_ButtonsDown(0) & PAD_BUTTON_A));
					entrypointFunc();

					/*
					The "old style"

					//printf("%08X%08X\n", *((unsigned int *)(CardBuffer)), *((unsigned int *)(CardBuffer+ 4)));
					//printf("%08X%08X\n", *((unsigned int *)(CardBuffer + 8)), *((unsigned int *)(CardBuffer + 12)));
					//printf("%08X%08X\n", *((unsigned int *)(CardBuffer + 16)), *((unsigned int *)(CardBuffer + 20)));
					//printf("%08X%08X\n", *((unsigned int *)(CardBuffer + 24)), *((unsigned int *)(CardBuffer + 28)));
					memcpy((void *)safeBufferLocationTrustMe, (CardBuffer + 0x50), sizeof(u8) * (SectorSize - 0x50));
					curLocation += SectorSize;
					for (; curLocation < 1170; curLocation += SectorSize) {
						//printf("%08X%08X\n", *((unsigned int *)(CardBuffer + i)), *((unsigned int *)(CardBuffer + 1 + 4)));
						CARD_Read(&CardFile, CardBuffer, SectorSize, curLocation);
						memcpy((void *)safeBufferLocationTrustMe, CardBuffer, sizeof(u8) * SectorSize);
					}
					CARD_Close(&CardFile);
					CARD_Unmount(CARD_SLOTA);
					free(CardBuffer);
					printf("Copy completed\n");
					printf("Press A to Locate DOL\n");

					do {
						PAD_ScanPads();
						if (PAD_ButtonsDown(0) & PAD_BUTTON_START) PSOreload();
						VIDEO_WaitVSync();
					} while (!(PAD_ButtonsDown(0) & PAD_BUTTON_A));
					
					// Struct from: https://github.com/emukidid/xenogcfork/blob/master/XenoShell/source/main.c#L336
					struct dol_s {
						unsigned long sec_pos[18];
						unsigned long sec_address[18];
						unsigned long sec_size[18];
						unsigned long bss_address, bss_size, entry_point;
					} dol = *((struct dol_s*)safeBufferLocationTrustMe);


					for (int i = 0; i < 18; i++) {
						u32 secAddr = dol.sec_address[i];
						secAddr &= 0xFFFFFF;
						u32 locatedAddr = secAddr + (safeCodeLocationTrustMe);
						u32 loadAddr = dol.sec_pos[i];
						u32 locatedLoadAddr = loadAddr + (safeBufferLocationTrustMe);
						printf("Section Location: %08lx done\n", secAddr);
						printf("Buffer Location: %08lx done\n", loadAddr);
						printf("Section Location Located: %08lx done\n", locatedAddr);
						printf("Buffer Location Located: %08lx done\n", locatedLoadAddr);
						printf("Section Size: %lu done\n", dol.sec_size[i]);
						do {
							PAD_ScanPads();
							if (PAD_ButtonsDown(0) & PAD_BUTTON_START) PSOreload();
							VIDEO_WaitVSync();
						} while (!(PAD_ButtonsDown(0) & PAD_BUTTON_A));
						//for (u32 offset = 0; offset < dol->sec_size[i]; offset++) {
						//	u8 byte = *(((u8 *)locatedLoadAddr) + offset);
						//	*(((u8 *)locatedAddr) + offset) = byte;
						//}
						memcpy((void *)locatedAddr, (void *)locatedLoadAddr, dol.sec_size[i]);

						printf("Section %d done\n", i);
						do {
							PAD_ScanPads();
							if (PAD_ButtonsDown(0) & PAD_BUTTON_START) PSOreload();
							VIDEO_WaitVSync();
						} while (!(PAD_ButtonsDown(0) & PAD_BUTTON_A));
					}

					u32 entryPoint = dol.entry_point;
					entryPoint &= 0xFFFFFF;
					entryPoint += safeCodeLocationTrustMe;
					void(*entrypointFunc)() = (void(*)())(entryPoint);

					printf("Entry Point: %08lX\n", entryPoint);
					printf("Press A to jump\n");

					do {
						PAD_ScanPads();
						if (PAD_ButtonsDown(0) & PAD_BUTTON_START) PSOreload();
						VIDEO_WaitVSync();
					} while (!(PAD_ButtonsDown(0) & PAD_BUTTON_A));
					entrypointFunc();
					*/
				}
				else {
					printf("Card Error: %d\n", CardError);
				}
				
				
			
			} else {
				printf("File not found ...\n");
				/*printf("writing test file ...\n");
				CardError = CARD_Create(CARD_SLOTB ,DemoFileName,SectorSize,&CardFile);

				if (0 == CardError) {
					time_t gc_time;
					gc_time = time(NULL);

					sprintf(CardBuffer,"This text was written by MemCardDemo\nat %s\n",ctime(&gc_time));

					CardError = CARD_Write(&CardFile,CardBuffer,SectorSize,0);
					CardError = CARD_Close(&CardFile);
				}*/
			}

			
		}
	}

}
