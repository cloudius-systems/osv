/* Make OSv crash to test the panic drivers
 *
 * Copyright (C) 2013 Nodalink, SARL.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
int main(int argc, char *argv[])
{
    char *ptr = nullptr;
    *ptr = 'A';
    return 0;
}
