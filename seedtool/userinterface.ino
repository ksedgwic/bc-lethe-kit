// Copyright © 2020 Blockchain Commons, LLC

#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>

#include "hardware.h"
#include "seed.h"
#include "userinterface.h"
#include "selftest.h"	// Used to fetch dummy data for UI testing.
#include "util.h"
#include "qrcode.h"
#include "ur.h"
#include "keystore.h"
#include "wally_address.h"

namespace userinterface_internal {

UIState g_uistate;
String g_rolls;
bool g_submitted;
String g_error_string;

Seed * g_master_seed = NULL;
BIP39Seq * g_bip39 = NULL;
SLIP39ShareSeq * g_slip39_generate = NULL;
SLIP39ShareSeq * g_slip39_restore = NULL;

int g_ndx = 0;		// index of "selected" word
int g_pos = 0;		// char position of cursor
int g_scroll = 0;	// index of scrolled window

int g_restore_slip39_selected;

int const Y_MAX = 200;

// FreeSansBold9pt7b
int const H_FSB9 = 16;	// height
int const YM_FSB9 = 6;	// y-margin

// FreeSansBold12pt7b
int const H_FSB12 = 20;	// height
int const YM_FSB12 = 9;	// y-margin

// FreeMonoBold9pt7b
int const H_FMB9 = 14;	// height
int const YM_FMB9 = 2;	// y-margin

// FreeMonoBold12pt7b
int const W_FMB12 = 14;	// width
int const H_FMB12 = 21;	// height
int const YM_FMB12 = 4;	// y-margin

bool clear_full_window = true;

// Pages
struct pg_show_address_t pg_show_address{0, ur};
struct pg_export_wallet_t pg_export_wallet{ur};
struct pg_derivation_path_t pg_derivation_path{true, SINGLE_NATIVE_SEGWIT, false};
struct pg_set_xpub_format_t pg_set_xpub_format{ur};
struct pg_xpub_menu_t pg_xpub_menu = {false};
struct pg_set_xpub_options_t pg_set_xpub_options = {false, false};
struct pg_set_seed_format_t pg_set_seed_format = {ur};
struct pg_set_slip39_format_t pg_set_slip39_format = {ur};

Point text_center(const char * txt) {
    int16_t tbx, tby; uint16_t tbw, tbh;
    Point p;
    g_display->getTextBounds(txt, 0, 0, &tbx, &tby, &tbw, &tbh);
    // center bounding box by transposition of origin:
    p.x = ((g_display->width() - tbw) / 2) - tbx;
    p.y = ((g_display->height() - tbh) / 2) - tby;
    return p;
}

int text_right(const char * txt) {
    int16_t tbx, tby; uint16_t tbw, tbh;
    g_display->getTextBounds(txt, 0, 0, &tbx, &tby, &tbw, &tbh);
    return ((g_display->width() - tbw) - tbx);
}

void full_window_clear() {
    g_display->firstPage();
    do
    {
        g_display->setFullWindow();
        g_display->fillScreen(GxEPD_WHITE);
    }
    while (g_display->nextPage());
}

void display_printf(const char *format, ...) {
    char buff[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buff, sizeof(buff), format, args);
    va_end(args);
    buff[sizeof(buff)/sizeof(buff[0])-1]='\0';
    g_display->print(buff);
}

void interstitial_error(String const lines[], size_t nlines) {
    serial_assert(nlines <= 7);

    int xoff = 16;
    int yoff = 6;

    g_display->firstPage();
    do
    {
        g_display->setPartialWindow(0, 0, 200, 200);
        // g_display->fillScreen(GxEPD_WHITE);
        g_display->setTextColor(GxEPD_BLACK);

        int xx = xoff;
        int yy = yoff;

        for (size_t ii = 0; ii < nlines; ++ii) {
            yy += H_FSB9 + YM_FSB9;
            g_display->setFont(&FreeSansBold9pt7b);
            g_display->setCursor(xx, yy);
            serial_printf("%s", lines[ii].c_str());
            display_printf("%s", lines[ii].c_str());
        }

        yy = 190; // Absolute, stuck to bottom
        g_display->setFont(&FreeSansBold9pt7b);
        g_display->setCursor(xx, yy);
        display_printf("%", GIT_DESCRIBE);
    }
    while (g_display->nextPage());

    while (true) {
        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("interstitial_error saw " + String(key));
        switch (key) {
        case '#':
            return;
        default:
            break;
        }
    }
}


/**
 *   @brief       draw a qr code
 *   @pre         g_display->firstPage();
 *   @post        while (g_display->nextPage());
 *   @param[in]   text: text to be qr encoded
 */
bool displayQR(char * text) {
    // source: https://github.com/arcbtc/koopa/blob/master/main.ino

    // auto detect best qr code size
    int qrSize = 10;
    int ec_lvl = 0;
    int const sizes[18][4] = {
                        /* https://github.com/ricmoo/QRCode */
                        /* 1 */ { 17, 14, 11, 7 },
                        /* 2 */ { 32, 26, 20, 14 },
                        /* 3 */ { 53, 42, 32, 24 },
                        /* 4 */ { 78, 62, 46, 34 },
                        /* 5 */ { 106, 84, 60, 44 },
                        /* 6 */ { 134, 106, 74, 58 },
                        /* 7 */ { 154, 122, 86, 64 },
                        /* 8 */ { 192, 152, 108, 84 },
                        /* 9 */ { 230, 180, 130, 98 },      // OPN:0 LND:0 good
                        /* 10 */ { 271, 213, 151, 119 },    // BTP:0 OPN:1 good
                        /* 11 */ { 321, 251, 177, 137 },
                        /* 12 */ { 367, 287, 203, 155 },    // BTP:1 bad
                        /* 13 */ { 425, 331, 241, 177 },    // BTP:2 meh
                        /* 14 */ { 458, 362, 258, 194 },
                        /* 15 */ { 520, 412, 292, 220 },
                        /* 16 */ { 586, 450, 322, 250 },
                        /* 17 */ { 644, 504, 364, 280 },
    };
    int len = strlen(text);
    for(int ii=0; ii<17; ii++){
        qrSize = ii+1;
        if(sizes[ii][ec_lvl] > len){
            break;
        }
    }

    // Create the QR code
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(qrSize)];
    qrcode_initText(&qrcode, qrcodeData, qrSize, ec_lvl,
                    text);

    int width = 17 + 4*qrSize;
    int scale = 130/width;
    int padding = (200 - width*scale)/2;

    // for every pixel in QR code we draw a rectangle with size `scale`
    for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
            if(qrcode_getModule(&qrcode, x, y)){
                g_display->fillRect(padding+scale*x,
                                   padding+scale*y,
                                   scale, scale, GxEPD_BLACK);
            }
        }
    }

    return true;
}

void self_test() {
    int xoff = 8;
    int yoff = 6;

    size_t const NLINES = 8;
    String lines[NLINES];

    // Turn the green LED on for the duration of the tests.
    hw_green_led(HIGH);

    bool last_test_passed = true;
    size_t numtests = selftest_numtests();
    // Loop, once for each test.  Need an extra trip at the end in
    // case we failed the last test.
    for (size_t ndx = 0; ndx < numtests+1; ++ndx) {

        // If any key is pressed, skip remaining self test.
        if (g_keypad.getKey() != NO_KEY)
            break;

        // Append each test name to the bottom of the displayed list.
        size_t row = ndx;
        if (row > NLINES - 1) {
            // slide all the lines up one
            for (size_t ii = 0; ii < NLINES - 1; ++ii)
                lines[ii] = lines[ii+1];
            row = NLINES - 1;
        }

        if (!last_test_passed) {
            lines[row] = "TEST FAILED";
        } else if (ndx < numtests) {
            lines[row] = selftest_testname(ndx).c_str();
        } else {
            // ran last test and it passed
            lines[row] = "TESTS PASSED";
        }

        // Display the current list.
        g_display->firstPage();
        do
        {
            g_display->setPartialWindow(0, 0, 200, 200);
            // g_display->fillScreen(GxEPD_WHITE);
            g_display->setTextColor(GxEPD_BLACK);

            int xx = xoff;
            int yy = yoff;

            yy += 1*(H_FSB9 + YM_FSB9);
            g_display->setFont(&FreeSansBold9pt7b);
            g_display->setCursor(xx, yy);
            g_display->println("Running self tests:");

            yy += 10;

            for (size_t ii = 0; ii < NLINES; ++ii) {
                yy += 1*(H_FMB9 + YM_FMB9);
                g_display->setFont(&FreeMonoBold9pt7b);
                g_display->setCursor(xx, yy);
                display_printf("%s", lines[ii].c_str());
            }

            yy = 190; // Absolute, stuck to bottom
            g_display->setFont(&FreeSansBold9pt7b);
            g_display->setCursor(xx, yy);
            display_printf("%", GIT_DESCRIBE);
        }
        while (g_display->nextPage());

        // If the test failed, abort (leaving status on screen).
        if (!last_test_passed) {
            hw_green_led(LOW);
            abort();
        }

        // If this isn't the last pass run the next test.
        if (ndx < numtests)
            last_test_passed = selftest_testrun(ndx);
    }
    delay(1000);		// short pause ..
    hw_green_led(LOW);	// Green LED back off until there is a seed.
    g_uistate = INTRO_SCREEN;
}

void intro_screen() {
    int xoff = 16;
    int yoff = 6;

    g_display->firstPage();
    do
    {
        g_display->setPartialWindow(0, 0, 200, 200);
        // g_display->fillScreen(GxEPD_WHITE);
        g_display->setTextColor(GxEPD_BLACK);

        int xx = xoff + 14;
        int yy = yoff + (H_FSB12 + YM_FSB12);
        g_display->setFont(&FreeSansBold12pt7b);
        g_display->setCursor(xx, yy);
        g_display->println("LetheKit v0");

        yy += 6;

        xx = xoff + 24;
        yy += H_FSB12 + YM_FSB12;
        g_display->setCursor(xx, yy);
        display_printf("Seedtool");

        xx = xoff + 50;
        yy += H_FSB9;
        g_display->setFont(&FreeSansBold9pt7b);
        g_display->setCursor(xx, yy);
        display_printf("%s", GIT_LATEST_TAG);

        xx = xoff + 28;
        yy += 1*(H_FSB9 + 2*YM_FSB9);
        g_display->setFont(&FreeSansBold9pt7b);
        g_display->setCursor(xx, yy);
        g_display->println("Blockchain");

        xx = xoff + 30;
        yy += H_FSB9 + 4;
        g_display->setCursor(xx, yy);
        g_display->println("Commons");

        xx = xoff + 18;
        yy += H_FSB9 + YM_FSB9 + 10;
        g_display->setCursor(xx, yy);
        g_display->println("Press any key");

        xx = xoff + 24;
        yy += H_FSB9;
        g_display->setCursor(xx, yy);
        g_display->println("to continue");
    }
    while (g_display->nextPage());

    while (true) {
        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("intro_screen saw " + String(key));
        g_uistate = SEEDLESS_MENU;
        return;
    }
}

void seedless_menu() {
    int xoff = 16;
    int yoff = 10;

    g_display->firstPage();
    do
    {
        g_display->setPartialWindow(0, 0, 200, 200);
        // g_display->fillScreen(GxEPD_WHITE);
        g_display->setTextColor(GxEPD_BLACK);

        int xx = xoff;
        int yy = yoff + (H_FSB12 + YM_FSB12);
        g_display->setFont(&FreeSansBold12pt7b);
        g_display->setCursor(xx, yy);
        g_display->println("No Seed");

        yy = yoff + 3*(H_FSB9 + YM_FSB9);
        g_display->setFont(&FreeSansBold9pt7b);
        g_display->setCursor(xx, yy);
        g_display->println("A - Generate Seed");
        yy += H_FSB9 + 2*YM_FSB9;
        g_display->setCursor(xx, yy);
        g_display->println("B - Restore BIP39");
        yy += H_FSB9 + 2*YM_FSB9;
        g_display->setCursor(xx, yy);
        // deprecating slip39. TODO Replace it with bc-shamir
        //g_display->println("C - Restore SLIP39");

        yy = 190; // Absolute, stuck to bottom
        g_display->setFont(&FreeSansBold9pt7b);
        g_display->setCursor(xx, yy);
        display_printf("%", GIT_DESCRIBE);
    }
    while (g_display->nextPage());

    while (true) {
        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("seedless_menu saw " + String(key));
        switch (key) {
        case 'A':
            g_uistate = GENERATE_SEED;
            return;
        case 'B':
            g_bip39 = new BIP39Seq();
            g_uistate = RESTORE_BIP39;
            return;
        // deprecating slip39. TODO Replace it with bc-shamir
        //case 'C':
        //    g_slip39_restore = new SLIP39ShareSeq();
        //    g_uistate = RESTORE_SLIP39;
        //    return;
        default:
            break;
        }
    }
}

void generate_seed() {
    while (true) {
        int xoff = 14;
        int yoff = 8;
        bool ret;

        g_display->firstPage();
        do
        {
            g_display->setPartialWindow(0, 0, 200, 200);
            // g_display->fillScreen(GxEPD_WHITE);
            g_display->setTextColor(GxEPD_BLACK);

            int xx = xoff;
            int yy = yoff + (H_FSB12 + YM_FSB12);
            g_display->setFont(&FreeSansBold12pt7b);
            g_display->setCursor(xx, yy);
            g_display->println("Generate Seed");

            yy += 10;

            yy += H_FSB9 + YM_FSB9;
            g_display->setFont(&FreeSansBold9pt7b);
            g_display->setCursor(xx, yy);
            g_display->println("Enter Dice Rolls");

            yy += 10;

            yy += H_FMB12 + YM_FMB12;
            g_display->setFont(&FreeMonoBold12pt7b);
            g_display->setCursor(xx, yy);
            display_printf("Rolls: %d\n", g_rolls.length());
            yy += H_FMB12 + YM_FMB12;
            g_display->setCursor(xx, yy);
            display_printf(" Bits: %0.1f\n", g_rolls.length() * 2.5850);

            // bottom-relative position
            xx = xoff + 10;
            yy = Y_MAX - 2*(H_FSB9 + YM_FSB9);
            g_display->setFont(&FreeSansBold9pt7b);
            g_display->setCursor(xx, yy);
            g_display->println("Press * to clear");
            yy += H_FSB9 + YM_FSB9;
            g_display->setCursor(xx, yy);
            g_display->println("Press # to submit");
        }
        while (g_display->nextPage());

        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("generate_seed saw " + String(key));
        switch (key) {
        case '1': case '2': case '3':
        case '4': case '5': case '6':
            g_rolls += key;
            break;
        case '*':
            g_rolls = "";
            break;
        case '#': {
            g_submitted = true;
            serial_assert(!g_master_seed);
            if (g_master_seed)
                delete g_master_seed;
            g_master_seed = Seed::from_rolls(g_rolls);
            g_master_seed->log();
            serial_assert(!g_bip39);
            if (g_bip39)
                delete g_bip39;
            g_bip39 = new BIP39Seq(g_master_seed);

            ret = keystore.update_root_key(g_bip39->mnemonic_seed, BIP39_SEED_LEN_512);
            if (ret == false) {
                g_uistate = ERROR_SCREEN;
                return;
            }
            digitalWrite(GREEN_LED, HIGH);		// turn on green LED
            g_uistate = DISPLAY_BIP39;
        }
            return;
        default:
            break;
        }
    }
}

void set_network() {
    int xoff = 5, yoff = 5;
    String title = "Set network";
    bool ret;
    g_display->firstPage();
    do
    {
        g_display->setPartialWindow(0, 0, 200, 200);
        // g_display->fillScreen(GxEPD_WHITE);
        g_display->setTextColor(GxEPD_BLACK);

        int xx = xoff;
        int yy = yoff + (H_FSB9 + YM_FSB9);
        g_display->setFont(&FreeSansBold9pt7b);
        Point p = text_center(title.c_str());
        g_display->setCursor(p.x, yy);
        g_display->println(title);

        yy += H_FSB9 + 2*YM_FSB9 + 15;
        g_display->setCursor(4*xx, yy);
        g_display->println("A: Regtest");

        yy += H_FSB9 + 2*YM_FSB9 + 5;
        g_display->setCursor(4*xx, yy);
        g_display->println("B: Testnet");


        yy += H_FSB9 + 2*YM_FSB9 + 5;
        g_display->setCursor(4*xx, yy);
        g_display->println("C: Mainnet");

        // bottom-relative position
        xx = xoff + 2;
        yy = Y_MAX - (H_FSB9) + 2;
        g_display->setFont(&FreeSansBold9pt7b);
        g_display->setCursor(xx, yy);
        g_display->println("*-Cancel");
    }
    while (g_display->nextPage());

    char key;
    do {
        key = g_keypad.getKey();
    } while (key == NO_KEY);
    g_uistate = DISPLAY_XPUBS;
    clear_full_window = false;
    switch (key) {
    case 'A':
        network.set_network(REGTEST);
        ret = keystore.update_root_key(g_bip39->mnemonic_seed, BIP39_SEED_LEN_512, network.get_network());
        if (ret == false) {
            g_uistate = ERROR_SCREEN;
            return;
        }
        if (keystore.is_standard_derivation_path())
            keystore.save_standard_derivation_path(NULL, network.get_network());
        return;
    case 'B':
        network.set_network(TESTNET);
        ret = keystore.update_root_key(g_bip39->mnemonic_seed, BIP39_SEED_LEN_512, network.get_network());
        if (ret == false) {
            g_uistate = ERROR_SCREEN;
            return;
        }
        if (keystore.is_standard_derivation_path())
            keystore.save_standard_derivation_path(NULL, network.get_network());
        return;
    case 'C':
        network.set_network(MAINNET);
        ret = keystore.update_root_key(g_bip39->mnemonic_seed, BIP39_SEED_LEN_512, network.get_network());
        if (ret == false) {
            g_uistate = ERROR_SCREEN;
            return;
        }
        if (keystore.is_standard_derivation_path())
            keystore.save_standard_derivation_path(NULL, network.get_network());
        return;
    case '*':
        return;
    default:
        g_uistate = SET_NETWORK;
        break;
    }
}

void seedy_menu() {
    int xoff = 16;
    int yoff = 10;

    g_display->firstPage();
    do
    {
        g_display->setPartialWindow(0, 0, 200, 200);
        // g_display->fillScreen(GxEPD_WHITE);
        g_display->setTextColor(GxEPD_BLACK);

        int xx = xoff;
        int yy = yoff + (H_FSB12 + YM_FSB12);
        g_display->setFont(&FreeSansBold12pt7b);
        g_display->setCursor(xx, yy);
        g_display->println("Seed Present");

        yy = yoff + 3*(H_FSB9 + YM_FSB9);
        g_display->setFont(&FreeSansBold9pt7b);
        g_display->setCursor(xx, yy);
        g_display->println("A - Display BIP39");
        // deprecating slip39. TODO replace it with bc-shamir
        //yy += H_FSB9 + 2*YM_FSB9;
        //g_display->setCursor(xx, yy);
        //g_display->println("B - Generate SLIP39");
        yy += H_FSB9 + 2*YM_FSB9;
        g_display->setCursor(xx, yy);
        g_display->println("C - Display XPUB");
        yy += H_FSB9 + 2*YM_FSB9;
        g_display->setCursor(xx, yy);
        g_display->println("D - Display seed");
        yy += H_FSB9 + 2*YM_FSB9;
        g_display->setCursor(xx, yy);
        g_display->println("3 - Open Wallet");

        yy = 190; // Absolute, stuck to bottom
        g_display->setFont(&FreeSansBold9pt7b);
        g_display->setCursor(xx, yy);
        display_printf("%", GIT_DESCRIBE);
    }
    while (g_display->nextPage());

    while (true) {
        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("seedy_menu saw " + String(key));
        switch (key) {
        case 'A':
            g_uistate = DISPLAY_BIP39;
            return;
        //case 'B':
        //    g_uistate = CONFIG_SLIP39;
        //    return;
        case 'C':
            clear_full_window = false;
            g_uistate = DISPLAY_XPUBS;
            return;
        case '3':
            clear_full_window = false;
            g_uistate = OPEN_WALLET;
            return;
        case 'D':
            clear_full_window = false;
            g_uistate = DISPLAY_SEED;
            return;
        case '*':
           ui_reset_into_state(SEEDLESS_MENU);
           g_uistate = SEEDLESS_MENU;
           return;
        default:
            break;
        }
    }
}

void display_bip39() {
    int const nwords = BIP39Seq::WORD_COUNT;
    int scroll = 0;

    while (true) {
        int const xoff = 12;
        int const yoff = 0;
        int const nrows = 5;

        g_display->firstPage();
        do
        {
            g_display->setPartialWindow(0, 0, 200, 200);
            // g_display->fillScreen(GxEPD_WHITE);
            g_display->setTextColor(GxEPD_BLACK);

            int xx = xoff;
            int yy = yoff + (H_FSB9 + YM_FSB9);
            g_display->setFont(&FreeSansBold9pt7b);
            g_display->setCursor(xx, yy);
            g_display->println("BIP39 Mnemonic");
            yy += H_FSB9 + YM_FSB9;

            yy += 6;

            g_display->setFont(&FreeMonoBold12pt7b);
            for (int rr = 0; rr < nrows; ++rr) {
                int wndx = scroll + rr;
                g_display->setCursor(xx, yy);
                display_printf("%2d %s", wndx+1, g_bip39->get_string(wndx).c_str());
                yy += H_FMB12 + YM_FMB12;
            }

            // bottom-relative position
            xx = xoff + 2;
            yy = Y_MAX - (H_FSB9) + 2;
            g_display->setFont(&FreeSansBold9pt7b);
            g_display->setCursor(xx, yy);
            g_display->println("1,7-Up,Down #-Done");
        }
        while (g_display->nextPage());

        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("display_bip39 saw " + String(key));
        switch (key) {
        case '1':
            if (scroll > 0)
                scroll -= 1;
            break;
        case '7':
            if (scroll < (nwords - nrows))
                scroll += 1;
            break;
        case '#':
            g_uistate = SEEDY_MENU;
            return;
        default:
            break;
        }
    }
}

// Append a character to a SLIP39 config value, check range.
String config_slip39_addkey(String str0, char key) {
    String newstr;
    if (str0 == " ")
        newstr = key;
    else
        newstr = str0 + key;
    int val = newstr.toInt();
    if (val == 0)	// didn't convert to integer somehow
        return str0;
    if (val < 1)	// too small
        return str0;
    if (val > 16)	// too big
        return str0;
    return newstr;
}

void config_slip39() {
    bool thresh_done = false;
    String threshstr = "3";
    String nsharestr = "5";

    while (true) {
        int xoff = 20;
        int yoff = 8;

        g_display->firstPage();
        do
        {
            g_display->setPartialWindow(0, 0, 200, 200);
            // g_display->fillScreen(GxEPD_WHITE);
            g_display->setTextColor(GxEPD_BLACK);

            int xx = xoff;
            int yy = yoff + (H_FSB9 + YM_FSB9);
            g_display->setFont(&FreeSansBold9pt7b);
            g_display->setCursor(xx, yy);
            g_display->println("Configure SLIP39");

            yy += 10;

            yy += H_FMB12 + 2*YM_FMB12;
            g_display->setFont(&FreeMonoBold12pt7b);
            g_display->setCursor(xx, yy);
            display_printf(" Thresh: %s", threshstr.c_str());

            if (!thresh_done) {
                int xxx = xx + (9 * W_FMB12);
                int yyy = yy - H_FMB12;
                g_display->fillRect(xxx,
                                   yyy,
                                   W_FMB12 * threshstr.length(),
                                   H_FMB12 + YM_FMB12,
                                   GxEPD_BLACK);
                g_display->setTextColor(GxEPD_WHITE);
                g_display->setCursor(xxx, yy);
                display_printf("%s", threshstr.c_str());
                g_display->setTextColor(GxEPD_BLACK);
            }

            yy += H_FMB12 + 2*YM_FMB12;
            g_display->setCursor(xx, yy);
            display_printf("NShares: %s", nsharestr.c_str());

            if (thresh_done) {
                int xxx = xx + (9 * W_FMB12);
                int yyy = yy - H_FMB12;
                g_display->fillRect(xxx,
                                   yyy,
                                   W_FMB12 * nsharestr.length(),
                                   H_FMB12 + YM_FMB12,
                                   GxEPD_BLACK);
                g_display->setTextColor(GxEPD_WHITE);
                g_display->setCursor(xxx, yy);
                display_printf("%s", nsharestr.c_str());
                g_display->setTextColor(GxEPD_BLACK);
            }

            // bottom-relative position
            xx = xoff + 2;
            yy = Y_MAX - (H_FSB9) + 2;
            g_display->setFont(&FreeSansBold9pt7b);
            g_display->setCursor(xx, yy);

            if (!thresh_done) {
                g_display->println("*-Clear    #-Next");
            } else {
                // If nsharestr field is empty its a prev
                if (nsharestr == " ")
                    g_display->println("*-Prev     #-Done");
                else
                    g_display->println("*-Clear    #-Done");
            }
        }
        while (g_display->nextPage());

        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("config_slip39 saw " + String(key));
        switch (key) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            if (!thresh_done)
                threshstr = config_slip39_addkey(threshstr, key);
            else
                nsharestr = config_slip39_addkey(nsharestr, key);
            break;
        case '*':
            if (!thresh_done) {
                threshstr = " ";
            } else {
                // If nsharestr field is empty its a prev
                if (nsharestr == " ")
                    thresh_done = false;
                else
                    nsharestr = " ";
            }
            break;
        case '#':
            if (!thresh_done) {
                thresh_done = true;
                break;
            } else {
                if (threshstr.toInt() > nsharestr.toInt()) {
                    // Threshold is greater than nshares, put the cursor
                    // back on the threshold.
                    thresh_done = false;
                    break;
                }
                // It's ok to generate multiple slip39 shares.
                if (g_slip39_generate)
                    delete g_slip39_generate;

                // This will take a few seconds; clear the screen
                // immediately to let the user know something is
                // happening ..
                full_window_clear();

                g_slip39_generate =
                    SLIP39ShareSeq::from_seed(g_master_seed,
                                              threshstr.toInt(),
                                              nsharestr.toInt(),
                                              hw_random_buffer);
                g_uistate = DISPLAY_SLIP39;
                return;
            }
        default:
            break;
        }
    }
}

void set_slip39_format() {
    int xoff = 5, yoff = 5;
    String title = "Set format";
    g_display->firstPage();
    do
    {
        g_display->setPartialWindow(0, 0, 200, 200);
        // g_display->fillScreen(GxEPD_WHITE);
        g_display->setTextColor(GxEPD_BLACK);

        int xx = xoff;
        int yy = yoff + (H_FSB9 + YM_FSB9);
        g_display->setFont(&FreeSansBold9pt7b);
        Point p = text_center(title.c_str());
        g_display->setCursor(p.x, yy);
        g_display->println(title);

        yy += H_FSB9 + 2*YM_FSB9 + 15;
        g_display->setCursor(4*xx, yy);
        g_display->println("A: Text");

        yy += H_FSB9 + 2*YM_FSB9 + 5;
        g_display->setCursor(4*xx, yy);
        g_display->println("B: UR");


        yy += H_FSB9 + 2*YM_FSB9 + 5;
        g_display->setCursor(4*xx, yy);
        g_display->println("C: QR-UR");

        // bottom-relative position
        xx = xoff + 2;
        yy = Y_MAX - (H_FSB9) + 2;
        g_display->setFont(&FreeSansBold9pt7b);
        g_display->setCursor(xx, yy);
        g_display->println("*-Cancel");
    }
    while (g_display->nextPage());

    char key;
    do {
        key = g_keypad.getKey();
    } while (key == NO_KEY);
    g_uistate = DISPLAY_SLIP39;
    clear_full_window = false;
    switch (key) {
    case 'A':
        pg_set_slip39_format.slip39_format = text;
        return;
    case 'B':
        pg_set_slip39_format.slip39_format = ur;
        return;
    case 'C':
        pg_set_slip39_format.slip39_format = qr_ur;
        return;
    case '*':
        return;
    default:
        g_uistate = SET_SLIP39_FORMAT;
        break;
    }
}

void display_slip39() {
    int const nwords = SLIP39ShareSeq::WORDS_PER_SHARE;
    int sharendx = 0;
    int scroll = 0;
    String ur_string;
    bool retval;

    while (true) {
        int xoff = 12;
        int yoff = 0;
        int nrows = 5;

        g_display->firstPage();
        do
        {
            g_display->setPartialWindow(0, 0, 200, 200);
            g_display->setTextColor(GxEPD_BLACK);

            int xx = xoff;
            int yy = yoff + (H_FSB9 + YM_FSB9);
            g_display->setFont(&FreeSansBold9pt7b);
            g_display->setCursor(xx, yy);
            display_printf("SLIP39 %d/%d",
                           sharendx+1, g_slip39_generate->numshares());
            yy += H_FSB9 + YM_FSB9;

            yy += 8;

            g_display->setFont(&FreeMonoBold12pt7b);

            if (pg_set_slip39_format.slip39_format == text) {
                for (int rr = 0; rr < nrows; ++rr) {
                    int wndx = scroll + rr;
                    char const * word =
                        g_slip39_generate->get_share_word(sharendx, wndx);
                    g_display->setCursor(xx, yy);
                    display_printf("%2d %s", wndx+1, word);
                    yy += H_FMB12 + YM_FMB12;
                }
            }
            else if (pg_set_slip39_format.slip39_format == qr_ur) {
                String ur;
                retval = ur_encode_slip39_share(g_slip39_generate, sharendx, ur);
                if (retval == false) {
                    g_uistate = ERROR_SCREEN;
                    return;
                }
                ur.toUpperCase();
                displayQR((char *)ur.c_str());
            }
            else {
                int xx = 0;
                yy = 50;
                g_display->setFont(&FreeMonoBold9pt7b);
                g_display->setCursor(0, yy);

                retval = ur_encode_slip39_share(g_slip39_generate, sharendx, ur_string);
                if (retval == false) {
                    g_uistate = ERROR_SCREEN;
                    return;
                }

                // @FIXME write function dependent on font style and size
                int scroll_strlen = 18;

                if ((scroll)*scroll_strlen >= (int)ur_string.length()) {
                    // don't scroll to infinity
                    scroll--;
                }

                for (int k = 0; k < nrows; ++k) {
                    g_display->setCursor(xx, yy);
                    display_printf("%s", ur_string.substring((k + scroll)*scroll_strlen, (1+k + scroll)*scroll_strlen).c_str());
                    yy += H_FMB12 + YM_FMB12;
                }
            }

            yy = 195; // Absolute, stuck to bottom
            g_display->setFont(&FreeMono9pt7b);

            String right_option = "# Done";
            if (sharendx < (int)(g_slip39_generate->numshares()-1))
                right_option = "# Next";
            int x_r = text_right(right_option.c_str());
            g_display->setCursor(x_r, yy);
            g_display->println(right_option);

            String left_option = "Back *";
            g_display->setCursor(0, yy);
            g_display->println(left_option);

            yy -= 15;

            right_option = "1Up/7Down";
            x_r = text_right(right_option.c_str());
            g_display->setCursor(x_r, yy);
            if (pg_set_slip39_format.slip39_format != qr_ur)
                g_display->println(right_option);

            left_option = "Form A";
            g_display->setCursor(0, yy);
            g_display->println(left_option);
        }
        while (g_display->nextPage());

        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("display_slip39 saw " + String(key));
        switch (key) {
        case 'A':
            g_uistate = SET_SLIP39_FORMAT;
            clear_full_window = false;
            scroll = 0;
            return;
        case '1':
            if (scroll > 0)
                scroll -= 1;
            break;
        case '7':
            if(pg_set_slip39_format.slip39_format == ur) {
                scroll++;
            }
            else {
                if (scroll < (nwords - nrows))
                    scroll += 1;
            }
            break;
        case '*':	// prev
            if (sharendx > 0) {
                --sharendx;
                scroll = 0;
            }
            break;
        case '#':	// next / done
            if (sharendx < (int)(g_slip39_generate->numshares()-1)) {
                ++sharendx;
                scroll = 0;
            } else {
                g_uistate = SEEDY_MENU;
                return;
            }
            break;
        default:
            break;
        }
    }
}

struct WordListState {
    int nwords;				// number of words in the mnemonic
    int nrefwords;			// number of words in the total word list

    int* wordndx;			// ref index for each word in list

    int nrows;				// number of words visible
    int selected;			// index of selected word
    int pos;				// char index of cursor
    int scroll;				// index of first visible word

    WordListState(int i_nwords, int i_nrefwords)
        : nwords(i_nwords)
        , nrefwords(i_nrefwords)
        , nrows(5)
        , selected(0)
        , pos(0)
        , scroll(0)
    {
        wordndx = (int*) malloc(i_nwords * sizeof(int));
    }

    ~WordListState() {
        free(wordndx);
    }

    virtual String refword(int ndx) = 0;

    void set_words(uint16_t const * wordlist) {
        for (int ii = 0; ii < nwords; ++ii)
            wordndx[ii] = wordlist ? wordlist[ii] : 0;
    }

    void get_words(uint16_t * o_wordlist) const {
        for (int ii = 0; ii < nwords; ++ii)
            o_wordlist[ii] = wordndx[ii];
    }

    void compute_scroll() {
        if (selected < 3)
            scroll = 0;
        else if (selected > nwords - 3)
            scroll = nwords - nrows;
        else
            scroll = selected - 2;
    }

    void select_prev() {
        if (selected == 0)
            selected = nwords - 1;
        else
            --selected;
        pos = 0;
    }

    void select_next() {
        if (selected == nwords - 1)
            selected = 0;
        else
            ++selected;
        pos = 0;
    }

    void cursor_left() {
        if (pos >= 1)
            --pos;
    }

    void cursor_right() {
        if (pos < (int)refword(wordndx[selected]).length() - 1)
            ++pos;
    }

    void word_down() {
        // Find the previous word that differs in the cursor position.
        int wordndx0 = wordndx[selected];	// remember starting wordndx
        String prefix0 = prefix(wordndx[selected]);
        char curr0 = current(wordndx[selected]);
        do {
            if (wordndx[selected] == 0)
                wordndx[selected] = nrefwords - 1;
            else
                --wordndx[selected];
            // If we've returned to the original there are no choices.
            if (wordndx[selected] == wordndx0)
                break;
        } while (prefix(wordndx[selected]) != prefix0 ||
                 current(wordndx[selected]) == curr0);
    }

    void word_up() {
        // Find the next word that differs in the cursor position.
        int wordndx0 = wordndx[selected];	// remember starting wordndx
        String prefix0 = prefix(wordndx[selected]);
        char curr0 = current(wordndx[selected]);
        do {
            if (wordndx[selected] == (nrefwords - 1))
                wordndx[selected] = 0;
            else
                ++wordndx[selected];
            // If we've returned to the original there are no choices.
            if (wordndx[selected] == wordndx0)
                break;
        } while (prefix(wordndx[selected]) != prefix0 ||
                 current(wordndx[selected]) == curr0);
    }

    String prefix(int word) {
        return refword(word).substring(0, pos);
    }

    char current(int word) {
        return refword(word)[pos];
    }

    bool unique_match() {
        // Does the previous word also match?
        if (wordndx[selected] > 0)
            if (prefix(wordndx[selected] - 1) == prefix(wordndx[selected]))
                return false;
        // Does the next word also match?
        if (wordndx[selected] < (nrefwords - 1))
            if (prefix(wordndx[selected] + 1) == prefix(wordndx[selected]))
                return false;
        return true;
    }
};

struct SLIP39WordlistState : WordListState {
    SLIP39WordlistState(int i_nwords) : WordListState(i_nwords, 1024) {}
    virtual String refword(int ndx) {
        return String(slip39_string_for_word(ndx));
    }
};

struct BIP39WordlistState : WordListState {
    BIP39Seq * bip39;
    BIP39WordlistState(BIP39Seq * i_bip39, int i_nwords)
        : WordListState(i_nwords, 2048)
        , bip39(i_bip39)
    {}
    virtual String refword(int ndx) {
        return BIP39Seq::get_dict_string(ndx);
    }
};

void restore_bip39() {
    BIP39WordlistState state(g_bip39, BIP39Seq::WORD_COUNT);
    state.set_words(NULL);

    while (true) {
        int const xoff = 12;
        int const yoff = 0;

        state.compute_scroll();

        g_display->firstPage();
        do
        {
            g_display->setPartialWindow(0, 0, 200, 200);
            // g_display->fillScreen(GxEPD_WHITE);
            g_display->setTextColor(GxEPD_BLACK);

            int xx = xoff;
            int yy = yoff + (H_FSB9 + YM_FSB9);
            g_display->setFont(&FreeSansBold9pt7b);
            g_display->setCursor(xx, yy);
            display_printf("BIP39 Mnemonic");
            yy += H_FSB9 + YM_FSB9;

            g_display->setFont(&FreeMonoBold12pt7b);
            yy += 2;

            for (int rr = 0; rr < state.nrows; ++rr) {
                int wndx = state.scroll + rr;
                String word = state.refword(state.wordndx[wndx]);
                Serial.println(String(wndx) + " " + word);

                if (wndx != state.selected) {
                    // Regular entry, not being edited
                    g_display->setTextColor(GxEPD_BLACK);
                    g_display->setCursor(xx, yy);
                    display_printf("%2d %s\n", wndx+1, word.c_str());
                } else {
                    // Edited entry
                    if (state.unique_match()) {
                        // Unique, highlight entire word.
                        g_display->fillRect(xx - 1,
                                           yy - H_FMB12 + YM_FMB12,
                                           W_FMB12 * (word.length() + 3) + 3,
                                           H_FMB12 + YM_FMB12,
                                           GxEPD_BLACK);

                        g_display->setTextColor(GxEPD_WHITE);
                        g_display->setCursor(xx, yy);

                        display_printf("%2d %s\n", wndx+1, word.c_str());

                    } else {
                        // Not unique, highlight cursor.
                        g_display->setTextColor(GxEPD_BLACK);
                        g_display->setCursor(xx, yy);

                        display_printf("%2d %s\n", wndx+1, word.c_str());

                        g_display->fillRect(xx + (state.pos+3)*W_FMB12,
                                           yy - H_FMB12 + YM_FMB12,
                                           W_FMB12,
                                           H_FMB12 + YM_FMB12,
                                           GxEPD_BLACK);

                        g_display->setTextColor(GxEPD_WHITE);
                        g_display->setCursor(xx + (state.pos+3)*W_FMB12, yy);
                        display_printf("%c", word.c_str()[state.pos]);
                    }
                }

                yy += H_FMB12 + YM_FMB12;
            }

            // bottom-relative position
            xx = xoff;
            yy = Y_MAX - 2*(H_FSB9) + 2;
            g_display->setFont(&FreeSansBold9pt7b);
            g_display->setTextColor(GxEPD_BLACK);
            g_display->setCursor(xx, yy);
            g_display->println("4,6-L,R 2,8-chr-,chr+");
            yy += H_FSB9 + 2;
            g_display->setCursor(xx, yy);
            g_display->println("1,7-Up,Down #-Done");
        }
        while (g_display->nextPage());

        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("restore bip39 saw " + String(key));
        switch (key) {
        case '1':
            state.select_prev();
            break;
        case '7':
            state.select_next();
            break;
        case '4':
            state.cursor_left();
            break;
        case '6':
            state.cursor_right();
            break;
        case '2':
            state.word_down();
            break;
        case '8':
            state.word_up();
            break;
        case 'D':
            // If 'D' and then '0' are typed, fill dummy data.
            do {
                key = g_keypad.getKey();
            } while (key == NO_KEY);
            Serial.println("restore bip39_D saw " + String(key));
            switch (key) {
            case '0':
                Serial.println("Loading dummy bip39 data");
                state.set_words(selftest_dummy_bip39());
                break;
            default:
                break;
            }
            break;
        case '#':	// done
            {
                uint16_t bip39_words[BIP39Seq::WORD_COUNT];
                for (size_t ii = 0; ii < BIP39Seq::WORD_COUNT; ++ii)
                    bip39_words[ii] = state.wordndx[ii];
                BIP39Seq * bip39 = BIP39Seq::from_words(bip39_words);
                Seed * seed = bip39->restore_seed();
                if (seed) {
                    serial_assert(!g_master_seed);
                    g_master_seed = seed;
                    g_master_seed->log();
                    delete g_bip39;
                    g_bip39 = bip39;
                    digitalWrite(GREEN_LED, HIGH);		// turn on green LED
                    g_uistate = SEEDY_MENU;
                    return;
                } else {
                    delete bip39;
                    String lines[7];
                    size_t nlines = 0;
                    lines[nlines++] = "BIP39 Word List";
                    lines[nlines++] = "Checksum Error";
                    lines[nlines++] = "";
                    lines[nlines++] = "Check your word";
                    lines[nlines++] = "list carefully";
                    lines[nlines++] = "";
                    lines[nlines++] = "Press # to revisit";
                    interstitial_error(lines, nlines);
                }
                break;
            }
            break;
        default:
            break;
        }
    }
}

void restore_slip39() {
    int scroll = 0;
    int selected = g_slip39_restore->numshares();	// selects "add" initially

    while (true) {
        int const xoff = 12;
        int const yoff = 0;
        int const nrows = 4;

        // Are we showing the restore action?
        int showrestore = g_slip39_restore->numshares() > 0 ? 1 : 0;

        // How many rows displayed?
        int disprows = g_slip39_restore->numshares() + 1 + showrestore;
        if (disprows > nrows)
            disprows = nrows;

        // Adjust the scroll to center the selection.
        if (selected < 2)
            scroll = 0;
        else if (selected > (int)g_slip39_restore->numshares())
            scroll = g_slip39_restore->numshares() + 2 - disprows;
        else
            scroll = selected - 2;
        serial_printf("scroll = %d\n", scroll);

        g_display->firstPage();
        do
        {
            g_display->setPartialWindow(0, 0, 200, 200);
            // g_display->fillScreen(GxEPD_WHITE);
            g_display->setTextColor(GxEPD_BLACK);

            int xx = xoff;
            int yy = yoff + (H_FSB9 + YM_FSB9);
            g_display->setFont(&FreeSansBold9pt7b);
            g_display->setCursor(xx, yy);
            g_display->println("Enter SLIP39 Shares");
            yy += H_FSB9 + YM_FSB9;

            xx = xoff + 20;
            yy += 16;

            g_display->setFont(&FreeMonoBold12pt7b);
            for (int rr = 0; rr < disprows; ++rr) {
                int sharendx = scroll + rr;
                char buffer[32];
                if (sharendx < (int)g_slip39_restore->numshares()) {
                    sprintf(buffer, "Share %d", sharendx+1);
                } else if (sharendx == (int)g_slip39_restore->numshares()) {
                    sprintf(buffer, "Add Share");
                } else {
                    sprintf(buffer, "Restore");
                }

                g_display->setCursor(xx, yy);
                if (sharendx != selected) {
                    g_display->println(buffer);
                } else {
                    g_display->fillRect(xx,
                                       yy - H_FMB12,
                                       W_FMB12 * strlen(buffer),
                                       H_FMB12 + YM_FMB12,
                                       GxEPD_BLACK);
                    g_display->setTextColor(GxEPD_WHITE);
                    g_display->println(buffer);
                    g_display->setTextColor(GxEPD_BLACK);
                }

                yy += H_FMB12 + YM_FMB12;
            }

            // bottom-relative position
            xx = xoff + 2;
            yy = Y_MAX - (H_FSB9) + 2;
            g_display->setFont(&FreeSansBold9pt7b);
            g_display->setCursor(xx, yy);
            g_display->println("1,7-Up,Down #-Do");
        }
        while (g_display->nextPage());

        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("restore_slip39 saw " + String(key));
        switch (key) {
        case '1':
            if (selected > 0)
                selected -= 1;
            break;
        case '7':
            if (selected < (int)g_slip39_restore->numshares() + 1 + showrestore - 1)
                selected += 1;
            break;
        case '*':
            g_uistate = SEEDLESS_MENU;
            return;
        case '#':
            if (selected < (int)g_slip39_restore->numshares()) {
                // Edit existing share
                g_restore_slip39_selected = selected;
                g_uistate = ENTER_SHARE;
                return;
            } else if (selected == (int)g_slip39_restore->numshares()) {
                // Add new (zeroed) share
                uint16_t share[SLIP39ShareSeq::WORDS_PER_SHARE] =
                    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
                g_restore_slip39_selected = g_slip39_restore->add_share(share);
                g_uistate = ENTER_SHARE;
                return;
            } else {
                // Attempt restoration

                // This will take a few seconds; clear the screen
                // immediately to let the user know something is
                // happening ..
                full_window_clear();

                for (size_t ii = 0; ii < g_slip39_restore->numshares(); ++ii) {
                    char * strings =
                        g_slip39_restore->get_share_strings(ii);
                    serial_printf("%d %s\n", ii+1, strings);
                    free(strings);
                }
                Seed * seed = g_slip39_restore->restore_seed();
                if (!seed) {
                    int err = g_slip39_restore->last_restore_error();
                    String lines[7];
                    size_t nlines = 0;
                    lines[nlines++] = "SLIP39 Error";
                    lines[nlines++] = "";
                    lines[nlines++] = SLIP39ShareSeq::error_msg(err);
                    lines[nlines++] = "";
                    lines[nlines++] = "";
                    lines[nlines++] = "Press # to revisit";
                    lines[nlines++] = "";
                    interstitial_error(lines, nlines);
                } else {
                    serial_assert(!g_master_seed);
                    g_master_seed = seed;
                    g_master_seed->log();
                    serial_assert(!g_bip39);
                    g_bip39 = new BIP39Seq(seed);
                    digitalWrite(GREEN_LED, HIGH);		// turn on green LED
                    g_uistate = DISPLAY_BIP39;
                    return;
                }
            }
            break;
        default:
            break;
        }
    }
}

void enter_share() {
    SLIP39WordlistState state(SLIP39ShareSeq::WORDS_PER_SHARE);
    state.set_words(g_slip39_restore->get_share(g_restore_slip39_selected));

    while (true) {
        int const xoff = 12;
        int const yoff = 0;

        state.compute_scroll();

        g_display->firstPage();
        do
        {
            g_display->setPartialWindow(0, 0, 200, 200);
            // g_display->fillScreen(GxEPD_WHITE);
            g_display->setTextColor(GxEPD_BLACK);

            int xx = xoff;
            int yy = yoff + (H_FSB9 + YM_FSB9);
            g_display->setFont(&FreeSansBold9pt7b);
            g_display->setCursor(xx, yy);
            display_printf("SLIP39 Share %d", g_restore_slip39_selected+1);
            yy += H_FSB9 + YM_FSB9;

            g_display->setFont(&FreeMonoBold12pt7b);
            yy += 2;

            for (int rr = 0; rr < state.nrows; ++rr) {
                int wndx = state.scroll + rr;
                String word = state.refword(state.wordndx[wndx]);
                Serial.println(String(wndx) + " " + word);

                if (wndx != state.selected) {
                    // Regular entry, not being edited
                    g_display->setTextColor(GxEPD_BLACK);
                    g_display->setCursor(xx, yy);
                    display_printf("%2d %s\n", wndx+1, word.c_str());
                } else {
                    // Edited entry
                    if (state.unique_match()) {
                        // Unique, highlight entire word.
                        g_display->fillRect(xx - 1,
                                           yy - H_FMB12 + YM_FMB12,
                                           W_FMB12 * (word.length() + 3) + 3,
                                           H_FMB12 + YM_FMB12,
                                           GxEPD_BLACK);

                        g_display->setTextColor(GxEPD_WHITE);
                        g_display->setCursor(xx, yy);

                        display_printf("%2d %s\n", wndx+1, word.c_str());

                    } else {
                        // Not unique, highlight cursor.
                        g_display->setTextColor(GxEPD_BLACK);
                        g_display->setCursor(xx, yy);

                        display_printf("%2d %s\n", wndx+1, word.c_str());

                        g_display->fillRect(xx + (state.pos+3)*W_FMB12,
                                           yy - H_FMB12 + YM_FMB12,
                                           W_FMB12,
                                           H_FMB12 + YM_FMB12,
                                           GxEPD_BLACK);

                        g_display->setTextColor(GxEPD_WHITE);
                        g_display->setCursor(xx + (state.pos+3)*W_FMB12, yy);
                        display_printf("%c", word.c_str()[state.pos]);
                    }
                }

                yy += H_FMB12 + YM_FMB12;
            }

            // bottom-relative position
            xx = xoff;
            yy = Y_MAX - 2*(H_FSB9) + 2;
            g_display->setFont(&FreeSansBold9pt7b);
            g_display->setTextColor(GxEPD_BLACK);
            g_display->setCursor(xx, yy);
            g_display->println("4,6-L,R 2,8-chr-,chr+");
            yy += H_FSB9 + 2;
            g_display->setCursor(xx, yy);
            g_display->println("1,7-Up,Down #-Done");
        }
        while (g_display->nextPage());

        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("enter_share saw " + String(key));
        switch (key) {
        case '1':
            state.select_prev();
            break;
        case '7':
            state.select_next();
            break;
        case '4':
            state.cursor_left();
            break;
        case '6':
            state.cursor_right();
            break;
        case '2':
            state.word_down();
            break;
        case '8':
            state.word_up();
            break;
        case 'D':
            do {
                key = g_keypad.getKey();
            } while (key == NO_KEY);
            Serial.println("enter_share_D saw " + String(key));
            switch (key) {
            case '0':
                // If 'D' and then '0' are typed, fill with valid dummy data.
                Serial.println("Loading dummy slip39 data");
                state.set_words(selftest_dummy_slip39(
                    g_restore_slip39_selected));
                break;
            case '9':
                // If 'D' and then '9' are typed, fill with invalid
                // share (but correct checksum).
                Serial.println("Loading dummy slip39 data");
                state.set_words(selftest_dummy_slip39_alt(
                    g_restore_slip39_selected));
                break;
            default:
                break;
            }
            break;
        case '*':
            // Don't add this share, go back to enter restore_slip39 menu.
            serial_assert(g_slip39_restore);
            g_slip39_restore->del_share(g_restore_slip39_selected);
            g_uistate = RESTORE_SLIP39;
            return;
        case '#':	// done
            {
                uint16_t words[SLIP39ShareSeq::WORDS_PER_SHARE];
                state.get_words(words);
                bool ok = SLIP39ShareSeq::verify_share_checksum(words);
                if (!ok) {
                    String lines[7];
                    size_t nlines = 0;
                    lines[nlines++] = "SLIP39 Share";
                    lines[nlines++] = "Checksum Error";
                    lines[nlines++] = "";
                    lines[nlines++] = "Check your word";
                    lines[nlines++] = "list carefully";
                    lines[nlines++] = "";
                    lines[nlines++] = "Press # to revisit";
                    interstitial_error(lines, nlines);
                } else {
                    serial_assert(g_slip39_restore);
                    g_slip39_restore->set_share(
                        g_restore_slip39_selected, words);
                    g_uistate = RESTORE_SLIP39;
                    return;
                }
            }
        default:
            break;
        }
    }
}

void derivation_path(void) {

    int x_off = 5;
    bool ret;

    while (true) {
      g_display->firstPage();
      do
      {
          g_display->setPartialWindow(0, 0, 200, 200);
          g_display->fillScreen(GxEPD_WHITE);
          g_display->setTextColor(GxEPD_BLACK);

          const char * title = "Choose derivation path";
          int yy = 20;

          g_display->setFont(&FreeSansBold9pt7b);
          Point p = text_center(title);
          g_display->setCursor(p.x, yy);
          g_display->println(title);

          g_display->setFont(&FreeMonoBold9pt7b);
          yy += 50;
          g_display->setCursor(x_off, yy);
          g_display->println("A: native segwit");

          yy += 30;
          g_display->setCursor(x_off, yy);
          g_display->println("B: nested segwit");

          yy += 30;
          g_display->setCursor(x_off, yy);
          g_display->println("C: custom");

          yy = 195; // Absolute, stuck to bottom
          g_display->setFont(&FreeMono9pt7b);
          String right_option = "Ok #";
          int x_r = text_right(right_option.c_str());
          g_display->setCursor(x_r, yy);
          g_display->println(right_option);

          String left_option = "Cancel *";
          g_display->setCursor(0, yy);
          g_display->println(left_option);
      }
      while (g_display->nextPage());

      char key;
      do {
          key = g_keypad.getKey();
      } while (key == NO_KEY);

      switch (key) {
        case '#':
            g_uistate = DISPLAY_XPUBS;
            return;
        case '*':
            g_uistate = DISPLAY_XPUBS;
            return;
        case 'A': {
            pg_derivation_path.std_derivation = SINGLE_NATIVE_SEGWIT;
            pg_derivation_path.is_standard_derivation = true;
            ret = keystore.save_standard_derivation_path(&pg_derivation_path.std_derivation, network.get_network());
            if (ret == false) {
                g_uistate = ERROR_SCREEN;
                return;
              }
            }
            g_uistate = DISPLAY_XPUBS;
            return;
        case 'B': {
            pg_derivation_path.std_derivation = SINGLE_NESTED_SEGWIT;
            pg_derivation_path.is_standard_derivation = true;
            ret = keystore.save_standard_derivation_path(&pg_derivation_path.std_derivation, network.get_network());
            if (ret == false) {
                g_uistate = ERROR_SCREEN;
                return;
              }
            }
            g_uistate = DISPLAY_XPUBS;
            return;
        case 'C':
            // slip132 option is not available for custom derivation paths
            pg_derivation_path.slip132 = false;
            pg_derivation_path.is_standard_derivation = false;
            g_uistate = CUSTOM_DERIVATION_PATH;
            return;
        default:
            break;
      }
    }
}

void custom_derivation_path(void) {

    String path_start = "m/";;
    String path_entered = keystore.derivation_path.substring(2);
    int x_off = 5;
    bool path_is_valid = true;
    String is_valid_tip = "Valid";

    while (true) {
      g_display->firstPage();
      do
      {
          g_display->setPartialWindow(0, 0, 200, 200);
          g_display->fillScreen(GxEPD_WHITE);
          g_display->setTextColor(GxEPD_BLACK);

          const char * title = "Enter derivation path";
          int yy = 20;

          g_display->setFont(&FreeSansBold9pt7b);
          Point p = text_center(title);
          g_display->setCursor(p.x, yy);
          g_display->println(title);

          g_display->setFont(&FreeMonoBold12pt7b);
          g_display->setCursor(x_off, yy + 50);
          g_display->println(path_start + path_entered);

          if (path_is_valid)
              is_valid_tip = "Valid";
          else
              is_valid_tip = "Invalid";
          p = text_center(is_valid_tip.c_str());
          g_display->setCursor(p.x, p.y + 20);
          g_display->println(is_valid_tip);

          yy = 195; // Absolute, stuck to bottom
          g_display->setFont(&FreeMono9pt7b);
          String right_option = "Ok #";
          int x_r = text_right(right_option.c_str());
          g_display->setCursor(x_r, yy);
          g_display->println(right_option);

          String left_option = "Cancel *";
          g_display->setCursor(0, yy);
          g_display->println(left_option);

          yy -= 25;
          String tip = "A=h  B=/";
          g_display->setCursor(0, yy);
          g_display->println(tip);

          right_option = "Del D";
          x_r = text_right(right_option.c_str());
          g_display->setCursor(x_r, yy);
          g_display->println(right_option);
      }
      while (g_display->nextPage());

      char key;
      do {
          key = g_keypad.getKey();
      } while (key == NO_KEY);

      switch (key) {
        case '#':
            if (path_is_valid) {
                bool ret = keystore.check_derivation_path((path_start + path_entered).c_str(), true);
                if (ret == false) {
                    g_uistate = ERROR_SCREEN;
                    return;
                }
                g_uistate = DISPLAY_XPUBS;
                return;
            }
            break;
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            path_entered += key;
            break;
        case 'A':
            path_entered += "h";
            break;
        case 'B':
            path_entered += "/";
            break;
        case 'D':
            if (path_entered.length() > 0)
                path_entered.remove(path_entered.length()-1);
            break;
        case '*':
            g_uistate = DERIVATION_PATH;
            return;
        default:
            break;
      }

      if (keystore.check_derivation_path((path_start + path_entered).c_str(), false)) {
          path_is_valid = true;
      }
      else {
          path_is_valid = false;
      }
    }
}

void set_xpub_format() {
    int xoff = 5, yoff = 5;
    String title = "Set xpub format";
    g_display->firstPage();
    do
    {
        g_display->setPartialWindow(0, 0, 200, 200);
        // g_display->fillScreen(GxEPD_WHITE);
        g_display->setTextColor(GxEPD_BLACK);

        int xx = xoff;
        int yy = yoff + (H_FSB9 + YM_FSB9);
        g_display->setFont(&FreeSansBold9pt7b);
        Point p = text_center(title.c_str());
        g_display->setCursor(p.x, yy);
        g_display->println(title);

        yy += H_FSB9 + 2*YM_FSB9 + 12;
        g_display->setCursor(4*xx, yy);
        g_display->println("A: Qr-Base58");

        yy += H_FSB9 + 2*YM_FSB9 + 1;
        g_display->setCursor(4*xx, yy);
        g_display->println("B: Base58");

        yy += H_FSB9 + 2*YM_FSB9 + 1;
        g_display->setCursor(4*xx, yy);
        g_display->println("C: Qr-UR");

        yy += H_FSB9 + 2*YM_FSB9 + 1;
        g_display->setCursor(4*xx, yy);
        g_display->println("D: UR");

        // bottom-relative position
        xx = xoff + 2;
        yy = Y_MAX - (H_FSB9) + 2;
        g_display->setFont(&FreeSansBold9pt7b);
        g_display->setCursor(xx, yy);
        g_display->println("*-Cancel");
    }
    while (g_display->nextPage());

    char key;
    do {
        key = g_keypad.getKey();
    } while (key == NO_KEY);
    g_uistate = DISPLAY_XPUBS;
    clear_full_window = false;
    switch (key) {
    case 'A':
        pg_set_xpub_format.xpub_format = qr_text;
        return;
    case 'B':
        pg_set_xpub_format.xpub_format = text;
        return;
    case 'C':
        pg_set_xpub_format.xpub_format = qr_ur;
        return;
    case 'D':
        pg_set_xpub_format.xpub_format = ur;
        return;
    case '*':
        return;
    default:
        g_uistate = SET_XPUB_FORMAT;
        break;
    }
}

void set_xpub_options() {
    int xoff = 5, yoff = 5;
    String title = "Slip132";
    g_display->firstPage();
    do
    {
        g_display->setPartialWindow(0, 0, 200, 200);
        // g_display->fillScreen(GxEPD_WHITE);
        g_display->setTextColor(GxEPD_BLACK);

        int xx = xoff;
        int yy = yoff + (H_FSB9 + YM_FSB9);
        g_display->setFont(&FreeSansBold9pt7b);
        Point p = text_center(title.c_str());
        g_display->setCursor(p.x, yy);
        g_display->println(title);

        yy += H_FSB9 + 2*YM_FSB9 + 5;
        g_display->setCursor(4*xx, yy);
        g_display->println("A: True    B: False");

        yy += H_FSB9 + 2*YM_FSB9 + 20;

        title = "Show path";
        p = text_center(title.c_str());
        g_display->setCursor(p.x, yy);
        g_display->println(title);

        yy += H_FSB9 + 2*YM_FSB9 + 5;
        g_display->setCursor(4*xx, yy);
        g_display->println("C: True    D: False");

        // bottom-relative position
        xx = xoff + 2;
        yy = Y_MAX - (H_FSB9) + 2;
        g_display->setFont(&FreeSansBold9pt7b);
        g_display->setCursor(xx, yy);
        g_display->println("*-Cancel");
    }
    while (g_display->nextPage());

    char key;
    do {
        key = g_keypad.getKey();
    } while (key == NO_KEY);
    g_uistate = DISPLAY_XPUBS;
    clear_full_window = false;
    switch (key) {
    case 'A':
        pg_set_xpub_options.slip132 = true;
        return;
    case 'B':
        pg_set_xpub_options.slip132 = false;
        return;
    case 'C':
        pg_set_xpub_options.show_derivation_path = true;
        return;
    case 'D':
        pg_set_xpub_options.show_derivation_path = false;
        return;
    case '*':
        return;
    default:
        g_uistate = SET_XPUB_FORMAT;
        break;
    }
}

void display_xpub(void) {
    ext_key key;
    String ur_string;
    String encoding_type;
    const int nrows = 5;
    int scroll = 0;
    String derivation_path = keystore.get_derivation_path();
    int scroll_strlen = 0;
    bool ret;

    ret = keystore.update_root_key(g_bip39->mnemonic_seed, BIP39_SEED_LEN_512);
    if (ret == false) {
        g_uistate = ERROR_SCREEN;
        return;
    }

    ret = keystore.get_xpub(&key);
    if (ret == false) {
        g_uistate = ERROR_SCREEN;
        return;
    }

    char *xpub = NULL;
    ret = keystore.xpub_to_base58(&key, &xpub, pg_set_xpub_options.slip132);
    if (ret == false) {
        g_uistate = ERROR_SCREEN;
        return;
    }

    ret = ur_encode_hd_pubkey_xpub(ur_string, keystore.derivation, keystore.derivationLen);
    if (ret == false) {
        g_uistate = ERROR_SCREEN;
        return;
    }

    while (true) {
      g_display->firstPage();
      do
      {
          g_display->setPartialWindow(0, 0, 200, 200);
          g_display->fillScreen(GxEPD_WHITE);
          g_display->setTextColor(GxEPD_BLACK);

          const char * title = "Master pubkey";
          int yy = 18;
          g_display->setFont(&FreeSansBold9pt7b);
          Point p = text_center(title);
          g_display->setCursor(p.x, yy);
          g_display->println(title);

          switch(pg_set_xpub_format.xpub_format) {
            case text:
                g_display->setFont(&FreeMonoBold9pt7b);
                g_display->setCursor(0, yy + 30);
                if (pg_set_xpub_options.show_derivation_path) {
                    char fingerprint[9] = {0};
                    sprintf(fingerprint, "%08x", (unsigned int)keystore.fingerprint);
                    g_display->println("[" + String(fingerprint) + keystore.get_derivation_path().substring(1) + "]" + String(xpub));
                }
                else
                    g_display->println(xpub);
                break;
            case qr_text:
                if (pg_set_xpub_options.show_derivation_path) {
                    char fingerprint[9] = {0};
                    sprintf(fingerprint, "%08x", (unsigned int)keystore.fingerprint);
                    String fing = "[" + String(fingerprint) + keystore.get_derivation_path().substring(1) + "]" + String(xpub);
                    displayQR((char *)fing.c_str());
                }
                else {
                    displayQR(xpub);
                }
                break;
            case ur: {
                int xx = 0;
                yy = 50;
                g_display->setFont(&FreeMonoBold9pt7b);
                g_display->setCursor(0, yy);
                int16_t tbx, tby; uint16_t tbw, tbh;
                size_t i = 0;

                scroll_strlen = 18;

                for (int k = 0; k < nrows; ++k) {
                    g_display->setCursor(xx, yy);
                    display_printf("%s", ur_string.substring((k + scroll)*scroll_strlen, (1+k + scroll)*scroll_strlen).c_str());
                    yy += H_FMB12 + YM_FMB12;
                }
                }
                break;
            case qr_ur:
                ur_string.toUpperCase();
                displayQR((char *)ur_string.c_str());
                break;
            default:
                break;
          }

          yy = 195; // Absolute, stuck to bottom
          g_display->setFont(&FreeMono9pt7b);
          String right_option = "# Done";
          int x_r = text_right(right_option.c_str());
          g_display->setCursor(x_r, yy);
          g_display->println(right_option);

          String left_option = "Form A";
          g_display->setCursor(0, yy);
          g_display->println(left_option);

         yy = 195 - 20;
         if (pg_set_xpub_format.xpub_format == ur) {
             g_display->setCursor(0, yy);
             left_option = "<-1/7->";
             g_display->println(left_option);
         }
         else if (pg_set_xpub_format.xpub_format == text || pg_set_xpub_format.xpub_format == qr_text) {
             left_option = "B Opt.";
             g_display->setCursor(0, yy);
             g_display->println(left_option);
         }

         right_option = "C Path";
         x_r = text_right(right_option.c_str());
         g_display->setCursor(x_r, yy);
         g_display->println(right_option);
      }
      while (g_display->nextPage());

      char key;
      do {
          key = g_keypad.getKey();
      } while (key == NO_KEY);

      switch (key) {
        case '#':
            g_uistate = SEEDY_MENU;
            if (xpub != NULL) {
              wally_free_string(xpub);
            }
            return;
        case '*':
            g_uistate = SEEDY_MENU;
            if (xpub != NULL) {
              wally_free_string(xpub);
            }
            return;
        case '1':
            if (scroll > 0)
                scroll -= 1;
            break;
        case '7':
            scroll += 1;
            break;
        case 'A':
            g_uistate = SET_XPUB_FORMAT;
            return;
        case 'B':
            g_uistate = SET_XPUB_OPTIONS;
            return;
        case 'C':
            g_uistate = DERIVATION_PATH;
            return;
        default:
            break;
      }
    }
  }

void display_seed(void) {
    String ur_string;

    bool ret = ur_encode_crypto_seed(g_master_seed->data, sizeof(g_master_seed->data), ur_string);
    if (ret == false) {
        g_uistate = ERROR_SCREEN;
        return;
    }

    while (true) {
      g_display->firstPage();
      do
      {
          g_display->setPartialWindow(0, 0, 200, 200);
          g_display->fillScreen(GxEPD_WHITE);
          g_display->setTextColor(GxEPD_BLACK);

          const char * title = "Seed";
          int yy = 25;
          g_display->setFont(&FreeSansBold9pt7b);
          Point p = text_center(title);
          g_display->setCursor(p.x, yy);
          g_display->println(title);

          switch(pg_set_seed_format.seed_format) {
            case ur:
                g_display->setFont(&FreeMonoBold9pt7b);
                g_display->setCursor(0, yy + 40);
                g_display->println(ur_string);
                break;
            case qr_ur:
                ur_string.toUpperCase();
                displayQR((char *)ur_string.c_str());
                break;
            default:
                break;
          }

          yy = 195; // Absolute, stuck to bottom
          g_display->setFont(&FreeMono9pt7b);
          String right_option = "# Done";
          int x_r = text_right(right_option.c_str());
          g_display->setCursor(x_r, yy);
          g_display->println(right_option);

          String left_option = "Back *";
          g_display->setCursor(0, yy);
          g_display->println(left_option);

          yy = 195 - 20;
          g_display->setCursor(0, yy);
          left_option = "Form A";
          g_display->println(left_option);
      }
      while (g_display->nextPage());

      char key;
      do {
          key = g_keypad.getKey();
      } while (key == NO_KEY);

      switch (key) {
        case '#':
            g_uistate = SEEDY_MENU;
            return;
        case '*':
            g_uistate = SEEDY_MENU;
            return;
        case 'A':
            g_uistate = SET_SEED_FORMAT;
            return;
        default:
            break;
      }
    }
  }

void set_seed_format() {
    int xoff = 5, yoff = 5;
    String title = "Set seed format";
    g_display->firstPage();
    do
    {
        g_display->setPartialWindow(0, 0, 200, 200);
        // g_display->fillScreen(GxEPD_WHITE);
        g_display->setTextColor(GxEPD_BLACK);

        int xx = xoff;
        int yy = yoff + (H_FSB9 + YM_FSB9);
        g_display->setFont(&FreeSansBold9pt7b);
        Point p = text_center(title.c_str());
        g_display->setCursor(p.x, yy);
        g_display->println(title);

        yy += H_FSB9 + 2*YM_FSB9 + 15;
        g_display->setCursor(4*xx, yy);
        g_display->println("A: ur");

        yy += H_FSB9 + 2*YM_FSB9 + 5;
        g_display->setCursor(4*xx, yy);
        g_display->println("B: qr-ur");

        // bottom-relative position
        xx = xoff + 2;
        yy = Y_MAX - (H_FSB9) + 2;
        g_display->setFont(&FreeSansBold9pt7b);
        g_display->setCursor(xx, yy);
        g_display->println("*-Cancel");
    }
    while (g_display->nextPage());

    char key;
    do {
        key = g_keypad.getKey();
    } while (key == NO_KEY);
    g_uistate = DISPLAY_SEED;
    clear_full_window = false;
    switch (key) {
    case 'A':
        pg_set_seed_format.seed_format = ur;
        return;
    case 'B':
        pg_set_seed_format.seed_format = qr_ur;
        return;
    case '*':
        return;
    default:
        g_uistate = SET_SEED_FORMAT;
        break;
    }
}

void error_screen(void) {
    while (true) {
      g_display->firstPage();
      do
      {
          g_display->setPartialWindow(0, 0, 200, 200);
          g_display->fillScreen(GxEPD_WHITE);
          g_display->setTextColor(GxEPD_BLACK);

          const char * title = "Error!";
          int yy = 30;
          g_display->setFont(&FreeSansBold12pt7b);
          Point p = text_center(title);
          g_display->setCursor(p.x, yy);
          g_display->println(title);

          if (g_error_string.length() > 0) {
              g_display->setFont(&FreeMonoBold9pt7b);
              g_display->setCursor(0, yy + 40);
              g_display->println(g_error_string);
          }

          yy = 195; // Absolute, stuck to bottom
          g_display->setFont(&FreeMono9pt7b);
          String right_option = "# Done";
          int x_r = text_right(right_option.c_str());
          g_display->setCursor(x_r, yy);
          g_display->println(right_option);
      }
      while (g_display->nextPage());

      char key;
      do {
          key = g_keypad.getKey();
      } while (key == NO_KEY);

      switch (key) {
        case '#':
            g_error_string = "";
            g_uistate = SEEDLESS_MENU;
            return;
        default:
            break;
      }
    }
  }

void open_wallet(void) {
    int xx = 0;
    
    while (true) {
      g_display->firstPage();
      do
      {
          g_display->setPartialWindow(0, 0, 200, 200);
          g_display->fillScreen(GxEPD_WHITE);
          g_display->setTextColor(GxEPD_BLACK);

          const char * title = "Wallet";
          int yy = 20;

          g_display->setFont(&FreeSansBold9pt7b);
          Point p = text_center(title);
          g_display->setCursor(p.x, yy);
          g_display->println(title);

          yy += 50;
          g_display->setCursor(xx, yy);
          g_display->println("A: show address");

          yy += 30;
          g_display->setCursor(xx, yy);
          g_display->println("B: export");

          yy = 195; // Absolute, stuck to bottom
          g_display->setFont(&FreeMono9pt7b);
          String right_option = "Ok #";
          int x_r = text_right(right_option.c_str());
          g_display->setCursor(x_r, yy);
          g_display->println(right_option);

          String left_option = "Cancel *";
          g_display->setCursor(0, yy);
          g_display->println(left_option);
      }
      while (g_display->nextPage());

      char key;
      do {
          key = g_keypad.getKey();
      } while (key == NO_KEY);

      clear_full_window = false;
      switch (key) {
        case '#':
            g_uistate = SEEDY_MENU;
            return;
        case '*':
            g_uistate = SEEDY_MENU;
            return;
        case 'A':
            g_uistate = SHOW_ADDRESS;
            return;
        case 'B':
            g_uistate = EXPORT_WALLET;
            return;
        default:
            break;
      }
    }
}

void show_address(void) {

    String title = "Address " + String(pg_show_address.addr_indx);
    struct ext_key child_key;
    struct ext_key child_key2;
    char *addr_segwit = NULL; // @TODO free
    // @TODO only single native segwit for now
    String child_path_str = network.get_network() == MAINNET ? "m/84h/0h/0h/0" : "m/84h/1h/0h/0";
    uint32_t child_path[10];
    uint32_t child_path_len;
    String address_family;


    keystore.calc_derivation_path(child_path_str.c_str(), child_path, child_path_len);

    while (true) {

      (void)bip32_key_from_parent_path(&keystore.root, child_path, child_path_len, BIP32_FLAG_KEY_PRIVATE, &child_key);
      (void)bip32_key_from_parent(&child_key, pg_show_address.addr_indx, BIP32_FLAG_KEY_PUBLIC, &child_key2);

      switch(network.get_network())
      {
        case MAINNET:
            address_family = "bc";
            break;
        case TESTNET:
            address_family = "tb";
            break;
        default:
            address_family = "bcrt";
            break;
      }

      (void)wally_bip32_key_to_addr_segwit(&child_key2, address_family.c_str(), 0, &addr_segwit);

      // prepare cbor/ur format
      uint8_t data[100];
      size_t data_written;
      String address_ur;
      (void)wally_addr_segwit_to_bytes(addr_segwit, address_family.c_str(), 0, data, sizeof(data), &data_written);
      // @FIXME check the spec if "including the version and data push opcode" is valid in cbor structure
      (void)ur_encode_address(data, data_written, address_ur);

      g_display->firstPage();
      do
      {
          g_display->setPartialWindow(0, 0, 200, 200);
          g_display->fillScreen(GxEPD_WHITE);
          g_display->setTextColor(GxEPD_BLACK);

          int yy = 25;
          g_display->setFont(&FreeSansBold9pt7b);
          Point p = text_center(title.c_str());
          g_display->setCursor(p.x, yy);
          g_display->println(title);

          g_display->setFont(&FreeSansBold9pt7b);
          g_display->setCursor(0, yy + 40);
          switch(pg_show_address.addr_format) {
            case text:
                g_display->println(addr_segwit);
                break;
            case qr_text:
                displayQR(addr_segwit);
                break;
            case qr_ur:
            {
                address_ur.toUpperCase();
                displayQR((char *)address_ur.c_str());
                break;
            }
            case ur:
                g_display->println(address_ur.c_str());
                break;
            default:
                break;
          }

          yy = 195; // Absolute, stuck to bottom
          g_display->setFont(&FreeMono9pt7b);
          String right_option = "# Done";
          int x_r = text_right(right_option.c_str());
          g_display->setCursor(x_r, yy);
          g_display->println(right_option);

          String left_option = "Back *";
          g_display->setCursor(0, yy);
          g_display->println(left_option);

          yy -= 15;
          right_option = "<-4/6->";
          x_r = text_right(right_option.c_str());
          g_display->setCursor(x_r, yy);
          g_display->println(right_option);

          left_option = "Form A";
          g_display->setCursor(0, yy);
          g_display->println(left_option);
      }
      while (g_display->nextPage());

      char key;
      do {
          key = g_keypad.getKey();
      } while (key == NO_KEY);

      switch (key) {
        case '#':
            g_uistate = OPEN_WALLET;
            return;
        case '*':
            g_uistate = OPEN_WALLET;
            return;
        case '6':
            pg_show_address.addr_indx++;
            title = "Address " + String(pg_show_address.addr_indx);
            break;
        case '4':
            if (pg_show_address.addr_indx > 0)
                pg_show_address.addr_indx--;
            title = "Address " + String(pg_show_address.addr_indx);
            break;
        case 'A':
            g_uistate = SET_ADDRESS_FORMAT;
            return;
        default:
            break;
      }
    }
}

void set_address_format(void) {

    String title = "Set address format";
    int xx = 5;

    while (true) {
      g_display->firstPage();
      do
      {
          g_display->setPartialWindow(0, 0, 200, 200);
          g_display->fillScreen(GxEPD_WHITE);
          g_display->setTextColor(GxEPD_BLACK);

          int yy = 25;
          g_display->setFont(&FreeSansBold9pt7b);
          Point p = text_center(title.c_str());
          g_display->setCursor(p.x, yy);
          g_display->println(title);
          yy += H_FSB9 + 2*YM_FSB9 + 10;

          g_display->setCursor(xx, yy);
          g_display->println("A: text");
          yy += H_FSB9 + 2*YM_FSB9;

          g_display->setCursor(xx, yy);
          g_display->println("B: qr");
          yy += H_FSB9 + 2*YM_FSB9;

          g_display->setCursor(xx, yy);
          g_display->println("C: ur");
          yy += H_FSB9 + 2*YM_FSB9;

          g_display->setCursor(xx, yy);
          g_display->println("D: qr-ur");


          yy = 195; // Absolute, stuck to bottom
          g_display->setFont(&FreeMono9pt7b);
          String right_option = "# Done";
          int x_r = text_right(right_option.c_str());
          g_display->setCursor(x_r, yy);
          g_display->println(right_option);

          String left_option = "Back *";
          g_display->setCursor(0, yy);
          g_display->println(left_option);
      }
      while (g_display->nextPage());

      char key;
      do {
          key = g_keypad.getKey();
      } while (key == NO_KEY);

      g_uistate = SHOW_ADDRESS;
      switch (key) {
        case '#':
            return;
        case '*':
            g_uistate = SEEDY_MENU;
            return;
        case 'A':
            pg_show_address.addr_format = text;
            return;
        case 'B':
            pg_show_address.addr_format = qr_text;
            return;
        case 'C':
            pg_show_address.addr_format = ur;
            return;
        case 'D':
            pg_show_address.addr_format = qr_ur;
            return;
        default:
            break;
      }
    }
}

void export_wallet(void) {

    String title = "Wallet";
    struct ext_key child_key;
    // TODO: currently only single native segwit wallet supported
    String child_path_str = network.get_network() == MAINNET ? "m/84h/0h/0h" : "m/84h/1h/0h";
    uint32_t child_path[10];
    uint32_t child_path_len;
    String address_family;
    char derivation_path_with_fingerprint[100] = {0};
    size_t nrows = 4;
    size_t scroll = 0;
    char *xpub_base58 = NULL;
    // prepare cbor/ur format
    uint8_t data[100];
    size_t data_written;
    String wallet_text;
    String wallet_ur;

    while (true) {

      keystore.calc_derivation_path(child_path_str.c_str(), child_path, child_path_len);
      (void)bip32_key_from_parent_path(&keystore.root, child_path, child_path_len, BIP32_FLAG_KEY_PRIVATE, &child_key);

      sprintf(derivation_path_with_fingerprint, "[%02x%02x%02x%02x%s]", ((uint8_t *)&keystore.fingerprint)[3], ((uint8_t *)&keystore.fingerprint)[2],
                                                ((uint8_t *)&keystore.fingerprint)[1], ((uint8_t *)&keystore.fingerprint)[0], child_path_str.substring(1).c_str());

      (void)bip32_key_to_base58(&child_key, BIP32_FLAG_KEY_PUBLIC, &xpub_base58);
      wallet_text = "wpkh(" + String(derivation_path_with_fingerprint) + String(xpub_base58) + ")";
      free(xpub_base58);

      uint32_t fingerprint;
      ((uint8_t *)&fingerprint)[0] = child_key.parent160[3];
      ((uint8_t *)&fingerprint)[1] = child_key.parent160[2];
      ((uint8_t *)&fingerprint)[2] = child_key.parent160[1];
      ((uint8_t *)&fingerprint)[3] = child_key.parent160[0];
      (void)ur_encode_output_descriptor(wallet_ur, child_path, child_path_len, fingerprint); // TODO this is parent fingerprint unlike above which is root fingerprint

      g_display->firstPage();
      do
      {
          g_display->setPartialWindow(0, 0, 200, 200);
          g_display->fillScreen(GxEPD_WHITE);
          g_display->setTextColor(GxEPD_BLACK);

          int yy = 25; int xx = 0;
          g_display->setFont(&FreeSansBold9pt7b);
          Point p = text_center(title.c_str());
          g_display->setCursor(p.x, yy);
          g_display->println(title);
          yy += H_FMB12 + YM_FMB12 + 15;

          g_display->setFont(&FreeMonoBold9pt7b);
          g_display->setCursor(0, yy + 40);

          switch(pg_export_wallet.wallet_format) {
            case text:
            {
                Serial.println(wallet_text);
                Serial.println(scroll);
                int scroll_strlen = 18;
                for (int k = 0; k < nrows; ++k) {
                    g_display->setCursor(xx, yy);
                    display_printf("%s", wallet_text.substring((k + scroll)*scroll_strlen, (1+k + scroll)*scroll_strlen).c_str());
                    yy += H_FMB12 + YM_FMB12;
                }
                break;
            }
            case qr_text:
                displayQR((char *)wallet_text.c_str());
                break;
            case qr_ur:
            {
                wallet_ur.toUpperCase();
                displayQR((char *)wallet_ur.c_str());
                break;
            }
            case ur:
            {
                Serial.println(wallet_ur);
                int scroll_strlen = 18;
                for (int k = 0; k < nrows; ++k) {
                    g_display->setCursor(xx, yy);
                    display_printf("%s", wallet_ur.substring((k + scroll)*scroll_strlen, (1+k + scroll)*scroll_strlen).c_str());
                    yy += H_FMB12 + YM_FMB12;
                }
                break;
            }
            default:
                break;
          }

          yy = 195; // Absolute, stuck to bottom
          g_display->setFont(&FreeMono9pt7b);
          String right_option = "# Done";
          int x_r = text_right(right_option.c_str());
          g_display->setCursor(x_r, yy);
          g_display->println(right_option);

          String left_option = "Back *";
          g_display->setCursor(0, yy);
          g_display->println(left_option);

          yy -= 15;
          right_option = "<-4/6->";
          x_r = text_right(right_option.c_str());
          g_display->setCursor(x_r, yy);
          g_display->println(right_option);

          left_option = "Form A";
          g_display->setCursor(0, yy);
          g_display->println(left_option);
      }
      while (g_display->nextPage());

      char key;
      do {
          key = g_keypad.getKey();
      } while (key == NO_KEY);

      switch (key) {
        case '#':
            g_uistate = OPEN_WALLET;
            return;
        case '*':
            g_uistate = OPEN_WALLET;
            return;
        case '6':
            scroll++;
            break;
        case '4':
            if (scroll > 0)
                scroll--;
            break;
        case 'A':
            g_uistate = SET_EXPORT_WALLET_FORMAT;
            scroll = 0;
            return;
        default:
            break;
      }
    }
}

void set_export_wallet_format(void) {

    String title = "Set wallet format";
    int xx = 5;

    while (true) {
      g_display->firstPage();
      do
      {
          g_display->setPartialWindow(0, 0, 200, 200);
          g_display->fillScreen(GxEPD_WHITE);
          g_display->setTextColor(GxEPD_BLACK);

          int yy = 25;
          g_display->setFont(&FreeSansBold9pt7b);
          Point p = text_center(title.c_str());
          g_display->setCursor(p.x, yy);
          g_display->println(title);
          yy += H_FSB9 + 2*YM_FSB9 + 10;

          g_display->setCursor(xx, yy);
          g_display->println("A: text");
          yy += H_FSB9 + 2*YM_FSB9;

          g_display->setCursor(xx, yy);
          g_display->println("B: qr");
          yy += H_FSB9 + 2*YM_FSB9;

          g_display->setCursor(xx, yy);
          g_display->println("C: ur");
          yy += H_FSB9 + 2*YM_FSB9;

          g_display->setCursor(xx, yy);
          g_display->println("D: qr-ur");


          yy = 195; // Absolute, stuck to bottom
          g_display->setFont(&FreeMono9pt7b);
          String right_option = "# Done";
          int x_r = text_right(right_option.c_str());
          g_display->setCursor(x_r, yy);
          g_display->println(right_option);

          String left_option = "Back *";
          g_display->setCursor(0, yy);
          g_display->println(left_option);
      }
      while (g_display->nextPage());

      char key;
      do {
          key = g_keypad.getKey();
      } while (key == NO_KEY);

      g_uistate = EXPORT_WALLET;
      switch (key) {
        case '#':
            return;
        case '*':
            g_uistate = SEEDY_MENU;
            return;
        case 'A':
            pg_export_wallet.wallet_format = text;
            return;
        case 'B':
            pg_export_wallet.wallet_format = qr_text;
            return;
        case 'C':
            pg_export_wallet.wallet_format = ur;
            return;
        case 'D':
            pg_export_wallet.wallet_format = qr_ur;
            return;
        default:
            break;
      }
    }
}

} // namespace userinterface_internal

void ui_reset_into_state(UIState state) {
    using namespace userinterface_internal;

    if (g_master_seed) {
        delete g_master_seed;
        g_master_seed = NULL;
    }
    if (g_bip39) {
        delete g_bip39;
        g_bip39 = NULL;
    }
    if (g_slip39_generate) {
        delete g_slip39_generate;
        g_slip39_generate = NULL;
    }
    if (g_slip39_restore) {
        delete g_slip39_restore;
        g_slip39_restore = NULL;
    }

    g_rolls = "";
    g_submitted = false;
    g_uistate = state;

    hw_green_led(LOW);
}

void ui_dispatch() {
    using namespace userinterface_internal;

    if (clear_full_window) {
        full_window_clear();
        clear_full_window = true;
    }

    switch (g_uistate) {
    case SELF_TEST:
        self_test();
        break;
    case INTRO_SCREEN:
        intro_screen();
        break;
    case SEEDLESS_MENU:
        seedless_menu();
        break;
    case GENERATE_SEED:
        generate_seed();
        break;
    case RESTORE_BIP39:
        restore_bip39();
        break;
    case RESTORE_SLIP39:
        restore_slip39();
        break;
    case ENTER_SHARE:
        enter_share();
        break;
    case SEEDY_MENU:
        seedy_menu();
        break;
    case DISPLAY_BIP39:
        display_bip39();
        break;
    case CONFIG_SLIP39:
        config_slip39();
        break;
    case DISPLAY_SLIP39:
        display_slip39();
        break;
    case DISPLAY_XPUBS:
        display_xpub();
        break;
    case DERIVATION_PATH:
        derivation_path();
        break;
    case SET_NETWORK:
        set_network();
        break;
    case SET_XPUB_FORMAT:
        set_xpub_format();
        break;
    case SET_XPUB_OPTIONS:
        set_xpub_options();
        break;
    case DISPLAY_SEED:
        display_seed();
        break;
    case CUSTOM_DERIVATION_PATH:
        custom_derivation_path();
        break;
    case ERROR_SCREEN:
        error_screen();
        break;
    case SET_SLIP39_FORMAT:
        set_slip39_format();
        break;
    case OPEN_WALLET:
        open_wallet();
        break;
    case SHOW_ADDRESS:
        show_address();
        break;
    case SET_ADDRESS_FORMAT:
        set_address_format();
        break;
    case EXPORT_WALLET:
        export_wallet();
        break;
    case SET_EXPORT_WALLET_FORMAT:
       set_export_wallet_format();
       break;
    case SET_SEED_FORMAT:
       set_seed_format();
       break;
    default:
        Serial.println("loop: unknown g_uistate " + String(g_uistate));
        break;
    }
}
