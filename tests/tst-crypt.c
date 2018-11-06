#define _XOPEN_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crypt.h>

char* concat_str(char* first_block, char* second_block) {
	static char tmp[10];
	memset(tmp, 0, sizeof(tmp));
	strcat(tmp, first_block);
	strcat(tmp, second_block);
	return tmp;
}

int main(void) {
	// http://man7.org/linux/man-pages/man3/crypt.3.html
	char password[] = "Ab(d3";
	char salt[] = "201802221723";
	char result1[] = "20338EPKt5xtY";
	char result2[] = "$1$Ab(d3$.SgUCw1AqIVAiXBaS6kzU.";
	char result3[] = "$5$Ab(d3$00nkgDrj2dM68IbqFTZPJ.a3mgai49mY.Ezq5ey0xY0";
	char result4[] = "$6$Ab(d3$TB6UIPV7sprvpcQh2esPr2/ye4FTp9lLft8yAj.2x/HcTXPwzGDxdK/tIF10DdVdV9Z2hhc3MaosUBS3fdueZ1";

	// 20338EPKt5xtY
	if (strcmp(result1, crypt(password, salt)) != 0) return -1;

	// $1$Ab(d3$.SgUCw1AqIVAiXBaS6kzU.
	if (strcmp(result2, crypt(password, concat_str((char*) "$1$", password))) != 0) return -2;

	// $5$Ab(d3$00nkgDrj2dM68IbqFTZPJ.a3mgai49mY.Ezq5ey0xY0
	if (strcmp(result3, crypt(password, concat_str((char*) "$5$", password))) != 0) return -3;

	// $6$Ab(d3$TB6UIPV7sprvpcQh2esPr2/ye4FTp9lLft8yAj.2x/HcTXPwzGDxdK/tIF10DdVdV9Z2hhc3MaosUBS3fdueZ1
	if (strcmp(result4, crypt(password, concat_str((char*) "$6$", password))) !=0) return -4;

	// encrypt
	// http://man7.org/linux/man-pages/man3/encrypt.3.html
	char key[64];
	char buf[64];
	char txt[9];
	int i, j;

	for (i = 0; i < 8; i++) {
		for (j = 0; j < 8; j++) {
			buf[i * 8 + j] = password[i] >> j & 1;
		}

		setkey(key);
	}

	encrypt(buf, 0);
	for (i = 0; i < 8; i++) {
		for (j = 0, txt[i] = '\0'; j < 8; j++) {
			txt[i] |= buf[i * 8 + j] << j;
		}

		txt[8] = '\0';
	}

	if (strcmp(password, txt) == 0) return -5;

	encrypt(buf, 1);
	for (i = 0; i < 8; i++) {
		for (j = 0, txt[i] = '\0'; j < 8; j++) {
			txt[i] |= buf[i * 8 + j] << j;
		}

		txt[8] = '\0';
	}

	return (strcmp(password, txt) == 0) ? 0 : -6;
}