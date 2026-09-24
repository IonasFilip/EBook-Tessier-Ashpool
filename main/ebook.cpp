#include <stdio.h>

#define PIN_CS    GPIO_NUM_8
#define PIN_SCK   GPIO_NUM_18
#define PIN_MISO  GPIO_NUM_16
#define PIN_MOSI  GPIO_NUM_17
#define MOUNT_POINT "/sdcard"

#include <ostream>

#include "tinyxml2.h"
using namespace tinyxml2;

#include <stdio.h>
#include <dirent.h>

#include "esp_log.h"

#include "driver/spi_master.h"
#include "driver/sdspi_host.h"

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

uint32_t readLittle(FILE *file, int len)
{
    uint32_t result = 0;
    uint8_t c;

    for (int i = 0; i < len; i++) {
        if (fread(&c, 1, 1, file) != 1) {
            return 0; // read error / EOF
        }

        result |= ((uint32_t)c << (8 * i));
    }

    return result;
}

uint32_t searchCD(FILE *file, const char *search)
{
    uint8_t c;

    // Go to the End Of Central Directory record
    fseek(file, -22, SEEK_END);

    if (readLittle(file, 4) != 0x06054b50) {
        printf("Not a valid ZIP EOCD\n");
        return 0;
    }

    // Skip: disk number, CD start disk, entries, CD size
    fseek(file, 12, SEEK_CUR);

    // Offset of Central Directory
    uint32_t index = readLittle(file, 4);

    fseek(file, index, SEEK_SET);

    while (readLittle(file, 4) == 0x02014b50) {

        char result[256];
        int result_len = 0;

        // Skip to filename length
        fseek(file, 24, SEEK_CUR);

        uint16_t filename_lencd = readLittle(file, 2);
        uint16_t fileextra_lencd = readLittle(file, 2);
        uint16_t filecom_lencd = readLittle(file, 2);

        // Skip to local header offset
        fseek(file, 8, SEEK_CUR);

        uint32_t offset = readLittle(file, 4);

        printf("%lu - offset\n", (unsigned long)offset);

        // Read filename
        for (int i = 0; i < filename_lencd; i++) {

            fread(&c, 1, 1, file);

            if (result_len < sizeof(result) - 1) {
                result[result_len++] = (char)c;
            }

            printf("%c", (char)c);
        }

        result[result_len] = '\0';

        printf(" - file name\n");

        // Skip extra field + comment
        fseek(file,
              fileextra_lencd + filecom_lencd,
              SEEK_CUR);

        // Did we find the file?
        if (strcmp(result, search) == 0) {
            printf("%lu - return offset\n",
                   (unsigned long)offset);

            return offset;
        }
    }

    return 0;
}


void *getFileData(FILE *file, const char *filename)
{
    uint8_t c;

    // Find the file's local header
    uint32_t offset = searchCD(file, filename);

    fseek(file, offset, SEEK_SET);

    // Check local file header signature
    if (readLittle(file, 4) != 0x04034b50) {
        printf("Not a valid ZIP local header\n");
        return NULL;
    }

    // Skip version, flags, compression method, time/date, CRC
    fseek(file, 14, SEEK_CUR);

    uint32_t file_c_len = readLittle(file, 4);
    uint32_t file_len   = readLittle(file, 4);

    printf("Compressed: %lu bytes\n", (unsigned long)file_c_len);
    printf("Uncompressed: %lu bytes\n", (unsigned long)file_len);

    uint16_t filename_len = readLittle(file, 2);
    uint16_t fileextra_len = readLittle(file, 2);

    // Read filename
    printf("File name: ");

    for (int i = 0; i < filename_len; i++) {
        fread(&c, 1, 1, file);
        printf("%c", (char)c);
    }

    printf("\n");

    // Skip extra field
    fseek(file, fileextra_len, SEEK_CUR);

    // Allocate space for compressed data
    void *fileData = malloc(file_c_len);

    if (fileData == NULL) {
        printf("Failed to allocate %lu bytes\n",
               (unsigned long)file_c_len);
        return NULL;
    }

    // Read compressed data
    if (fread(fileData, 1, file_c_len, file) != file_c_len) {
        printf("Failed to read file data\n");
        free(fileData);
        return NULL;
    }

    return fileData;
}

char *getName(FILE *file, const char *filename) {
    uint8_t c;

    // Find the file's local header
    uint32_t offset = searchCD(file, filename);

    fseek(file, offset, SEEK_SET);

    // Check local file header signature
    if (readLittle(file, 4) != 0x04034b50) {
        printf("Not a valid ZIP local header\n");
    }

    // Skip version, flags, compression method, time/date, CRC
    fseek(file, 14, SEEK_CUR);

    uint32_t file_c_len = readLittle(file, 4);
    uint32_t file_len   = readLittle(file, 4);

    printf("Compressed: %lu bytes\n", (unsigned long)file_c_len);
    printf("Uncompressed: %lu bytes\n", (unsigned long)file_len);

    uint16_t filename_len = readLittle(file, 2);
    uint16_t fileextra_len = readLittle(file, 2);

    // Read filename
    printf("File name: ");

    char *bookName;

    for (int i = 0; i < filename_len; i++) {
        fread(&c, 1, 1, file);
        bookName += (char)c;
    }

    printf(bookName);
    return bookName;
}

void addBook(XMLDocument &doc, const char *filename) {
    XMLDocument loc;

    FILE *file = fopen(("/sdcard/books/" + std::string(filename)).c_str(), "rb");

    // doc.Parse(getFileData(file, "META-INF/container.xml").c_str());

    XMLElement *library = doc.FirstChildElement("library");

    if (library == nullptr) {
        printf("No <library> element!\n");
        return;
    }

    char *contentPath[256];
    char *bookName[256];
    char *author[256];

    XMLElement *book = doc.NewElement("book");
    book->SetAttribute("filename", filename);

    XMLElement *titleElement = doc.NewElement("title");
    titleElement->SetText("tbd");
    book->InsertEndChild(titleElement);

    library->InsertEndChild(book);

    doc.SaveFile("/sdcard/meta/library.xml");
}

void createIndex(XMLDocument &doc) {

    XMLElement *library = doc.NewElement("library");
    doc.InsertFirstChild(library);

    doc.SaveFile("/sdcard/meta/library.xml");

}

void checkIndex(DIR *library, XMLDocument &doc) {
    struct dirent *entry;

    XMLElement *libraryElement = doc.FirstChildElement("library");

    if (libraryElement == nullptr) {
        printf("No <library> element found in index!\n");
        return;
    }

    while ((entry = readdir(library)) != NULL) {
        const char *filename = entry->d_name;

        printf("Checking: %s\n", filename);
        XMLElement *book = libraryElement->FirstChildElement("book");

        bool found = false;

        while (book != nullptr) {

            const char *indexedFilename = book->Attribute("filename");

            if (indexedFilename != nullptr &&
                strcmp(filename, indexedFilename) == 0) {

                found = true;
                break;
                }

            book = book->NextSiblingElement("book");
        }

        if (found) {
            printf("  Already in index\n");
        } else {
            printf("  NOT in index\n");
            printf("  Adding\n");
            addBook(doc, filename);
        }
    }

    doc.SaveFile("/sdcard/meta/library.xml");
}

sdmmc_card_t *card;

static const char *TAG = "example";

void init() {
    esp_err_t ret;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(
     static_cast<spi_host_device_t>(host.slot),
     &bus_cfg,
     SDSPI_DEFAULT_DMA
 );

    if (ret != ESP_OK) {
        printf("SPI init failed: %s\n", esp_err_to_name(ret));
        return;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_CS;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    printf("Mounting SD card...\n");

    ret = esp_vfs_fat_sdspi_mount(
        MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &card
    );

    if (ret != ESP_OK) {
        printf("SD mount failed: %s\n", esp_err_to_name(ret));
        return;
    }

    printf("SD card mounted!\n");
}

extern "C" void app_main(void) {

    size_t psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    printf("PSRAM total: %u bytes\n", (unsigned)psram);

    if (psram > 0) {
        printf("PSRAM detected!\n");
    } else {
        printf("No PSRAM detected.\n");
    }

    uint8_t test = 10*100/10 + 5 - 20 * 20;

    printf("%u\n", test);

    init();

    XMLDocument doc;

    FILE *index = fopen("/sdcard/meta/library.xml", "r");

    if (index == NULL) {
        createIndex(doc);
        printf("Index Created\n");
    } else {
        printf("Index found\n");
        fclose(index);
        XMLError err = doc.LoadFile("/sdcard/meta/library.xml");

        if (err != XML_SUCCESS) {
            printf("Failed to load index: %s\n", doc.ErrorStr());
            return;
        }
    }

    DIR *dir = opendir("/sdcard/books");

    if (dir == NULL) {
        printf("Failed to open /books\n");
        return;
    }

    checkIndex(dir, doc);

    // FILE *file = fopen("/sdcard/books/SPRAWL~1.EPU", "rb");
    // XMLDocument doc;
    //
    //
    //
    // if (file != NULL) {
    //     printf("open /sdcard/books/Sprawl 1.epub\n");
    //     getFileData(file, "");
    // }else {
    //     printf("failed to open /sdcard/books/Sprawl 1.epub\n");
    // }
    // // XMLError err = doc.Parse(getFileData(file, "META-INF/container.xml").c_str());
    // // if (err != XML_SUCCESS) {
    // //     std::cerr << "XML parse error\n";
    // // }

    closedir(dir);

}
