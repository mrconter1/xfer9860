/***************************************************************************
 *   xfer9860 - Transfer files between a Casio fx-9860G and computer	   *
 *									   *
 *   Copyright (C) 2007							   *
 *	Manuel Naranjo <naranjo.manuel@gmail.com>			   *
 *	Andreas Bertheussen <andreasmarcel@gmail.com>			   *
 *   Copyright (C) 2004							   *
 *	Greg Kroah-Hartman <greg@kroah.com>				   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "usbio.h"
#include "Casio9860.h"

#include <usb.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/* Macro for nulling freed pointers */
#define FREE(p)   do { free(p); (p) = NULL; } while(0)

#define __DUMP__
#define LEN_LINE 16
#define MAX_VERIFICATION_ATTEMPTS 3

void debug(int input, char* array, int len){
#ifdef __DEBUG__
#endif //__DEBUG__
#ifdef __DUMP__
	unsigned char temp;
	int i, j, line = 0;

	if (input) { fprintf(stderr, "<< "); }
	else { fprintf(stderr, ">> "); }
	
	for (i = 0 ; i < len ; i++){
		temp = (unsigned char) array[i];
		
		if (i % LEN_LINE == 0 && i != 0){
			fprintf(stderr, "\t");
			for (j = line; j < line + LEN_LINE; j++){
				char u = (unsigned char) array[j];
				if (u > 31) { fprintf(stderr, "%c", u); }
				else { fprintf(stderr, "."); }
			}

			line = i;
			fprintf(stderr,"\n");
			if (input) { fprintf(stderr, "<< "); }
			else { fprintf(stderr, ">> "); }
		}
		
		fprintf(stderr,"%02X ", temp);
	}

	if (i % LEN_LINE != 0)	
		for (j = 0 ; j < (int)(LEN_LINE-(i % LEN_LINE)); j++)
			fprintf(stderr,"   ");
	
	fprintf(stderr, "\t");
	for (j = line; j < len; j++){
		temp = (short unsigned int) array[j];
		if (temp > 31)
			fprintf(stderr, "%c", temp);
		else
			fprintf(stderr, ".");			
	}

	fprintf(stderr,"\n\n");
#endif //__DUMP__
}

int main(int argc, char *argv[]) {
	int ret = 1;
	char *buffer, *temp;

	if (argc < 2) {
		printf(	"Upload a file to the fx-9860 by USB\n"
			"Usage:\t%s <filename>\n", argv[0]);
		return 0;
	}
	FILE *sourcefile = fopen(argv[1], "r");
	if (sourcefile == NULL) {
		printf("[E] Unable to open file: %s\n", argv[1]);
		goto exit;
	}

	struct stat file_status;
	stat(argv[1], &file_status);	// gets filesize

	printf(	"[I]  Found file: %s\n"
		"     Filesize:   %i byte(s)\n", argv[1], file_status.st_size);
	
	printf("[>] Opening connection...\n");
	struct usb_device *usb_dev;
	struct usb_dev_handle *usb_handle;

	usb_dev = (struct usb_device*)device_init();
	if (usb_dev == NULL) {
		fprintf(stderr,	"[E] The device cannot be found,\n"
				"    Make sure it is connected; press [ON], [MENU], [sin], [F2]\n");
		goto exit;
	}

	usb_handle = (struct usb_dev_handle*)usb_open(usb_dev);
	if (usb_handle == NULL) {
		fprintf(stderr, "[E] Unable to open the USB device. Exiting.\n");
		goto exit_close;
	}

	ret = usb_claim_interface(usb_handle, 0);
	if (ret < 0) {
		fprintf(stderr, "[E] Unable to claim USB Interface. Exiting.\n");
		goto exit_unclaim;
	}

	ret = init_9860(usb_handle);
	if (ret < 0){
		fprintf(stderr, "[E] Couldn't initialize connection. Exiting.\n");
		goto exit_unclaim;
	}
	printf("[I]  Connected.\n");
	
	/*
	 * TODO:
	 *	Check free space on device and alert user if too small
	 *	Transfer file
	 */
	
	// ================
	buffer = (char*)calloc(0x40, sizeof(char));
	if (buffer == NULL) {
		printf("[E] Out of memory. Exiting.\n");
		goto exit;
	}
	
	int i;
	for(i = 1; i <= MAX_VERIFICATION_ATTEMPTS; i++) {
		printf("[>] Verifying device, attempt %i...\n", i);
		memcpy(buffer, "\x05\x30\x30\x30\x37\x30", 6);
		ret = WriteUSB(usb_handle, buffer, 6);
		debug(0, buffer, ret);
		
		ret = ReadUSB(usb_handle, buffer, 6);
		debug(1, buffer, ret);
		if (buffer[0] == 0x06) {	/* lazy check */
			printf("[I]  Got verification response.\n");
			break;
		} else {
			if (i == MAX_VERIFICATION_ATTEMPTS) {
				printf("[E] Did not receive verification response. Exiting.\n");
				goto exit;
			}
			sleep(1);
		}
	}
	
	/*
	 * Request free RAM space transmission
	 * this will only check ram capacity, as we lack documentation and logs to check
	 * flash capacity, which is what we need here.. It is basically the same, just with
	 * another subtype and another offset for the returned size.
	 */
	printf("[>] Getting RAM capacity information...\n");
	memcpy(buffer, "\x01\x32\x42\x30\x35\x43", 6);
	ret = WriteUSB(usb_handle, buffer, 6);
	debug(0, buffer, ret);
	
	ret = ReadUSB(usb_handle, buffer, 6);
	debug(1, buffer, ret);
	if(buffer[0] != 0x06) {
		printf("[E] Error requesting capacity information. Exiting.\n");
		goto exit;
	}
	/* change direction */
	memcpy(buffer, "\x03\x30\x30\x30\x37\x30", 6);
	ret = WriteUSB(usb_handle, buffer, 6);
	debug(0, buffer, ret);
	
	/* expect free space transmission and ack */
	ret = ReadUSB(usb_handle, buffer, 0x22);
	debug(1, buffer, ret);
	if (buffer[0] == 0x01) {	/* lazy check */
		temp = (char*)calloc(8, sizeof(char));
		memcpy(temp, buffer+12, 8);
		printf("[I]  Free space in RAM: %i byte(s).\n", strtol(temp, NULL, 16));
		/* TODO: compare free space with filesize, possibly store free space somewhere neat */
		FREE(temp);
	} else {
		printf("[E] Did not receive acknowledgement. Exiting.\n");
		goto exit;
	}
	
	memcpy(buffer, "\x06\x30\x30\x30\x37\x30", 6);
	ret = WriteUSB(usb_handle, buffer, 6);
	debug(0, buffer, ret);


	/* end communication */
	printf("[>] Closing connection.\n");
	memcpy(buffer, "\x18\x30\x31\x30\x36\x46", 6);
	ret = WriteUSB(usb_handle, buffer, 6);
	debug(0, buffer, ret);

	// ====================
	exit_unclaim:
		usb_release_interface(usb_handle, 0);
	exit_close:
		usb_close(usb_handle);
		FREE(usb_dev);
		FREE(buffer);
	exit:

	return 0;
}
