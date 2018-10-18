#include "mbed.h"
#include "EasyAttach_CameraAndLCD.h"
#include "SdUsbConnect.h"
#include "JPEG_Converter.h"
#include "dcache-control.h"

/**** User Selection *********/
#define SAVA_FILE_TYPE         (0)     /* Select  0(Image(.jpg)) or 1(Movie(.avi)) */
/*****************************/

#define MOUNT_NAME             "storage"

/*! Frame buffer stride: Frame buffer stride should be set to a multiple of 32 or 128
    in accordance with the frame buffer burst transfer mode. */
#define VIDEO_PIXEL_HW         (640u)  /* VGA */
#define VIDEO_PIXEL_VW         (480u)  /* VGA */

#define FRAME_BUFFER_STRIDE    (((VIDEO_PIXEL_HW * 2) + 31u) & ~31u)
#define FRAME_BUFFER_HEIGHT    (VIDEO_PIXEL_VW)

#if defined(__ICCARM__)
#pragma data_alignment=32
static uint8_t user_frame_buffer0[FRAME_BUFFER_STRIDE * FRAME_BUFFER_HEIGHT]@ ".mirrorram";
#else
static uint8_t user_frame_buffer0[FRAME_BUFFER_STRIDE * FRAME_BUFFER_HEIGHT]__attribute((section("NC_BSS"),aligned(32)));
#endif
static int file_name_index = 1;
static volatile int Vfield_Int_Cnt = 0;
/* jpeg convert */
static JPEG_Converter Jcu;
#if defined(__ICCARM__)
#pragma data_alignment=32
static uint8_t JpegBuffer[1024 * 63];
#else
static uint8_t JpegBuffer[1024 * 63]__attribute((aligned(32)));
#endif

DisplayBase Display;
DigitalIn   button0(USER_BUTTON0);
DigitalOut  led1(LED1);

static void IntCallbackFunc_Vfield(DisplayBase::int_type_t int_type) {
    if (Vfield_Int_Cnt > 0) {
        Vfield_Int_Cnt--;
    }
}

static void wait_new_image(void) {
    Vfield_Int_Cnt = 1;
    while (Vfield_Int_Cnt > 0) {
        Thread::wait(1);
    }
}

static void Start_Video_Camera(void) {
    // Initialize the background to black
    for (uint32_t i = 0; i < sizeof(user_frame_buffer0); i += 2) {
        user_frame_buffer0[i + 0] = 0x10;
        user_frame_buffer0[i + 1] = 0x80;
    }

    // Field end signal for recording function in scaler 0
    Display.Graphics_Irq_Handler_Set(DisplayBase::INT_TYPE_S0_VFIELD, 0, IntCallbackFunc_Vfield);

    // Video capture setting (progressive form fixed)
    Display.Video_Write_Setting(
        DisplayBase::VIDEO_INPUT_CHANNEL_0,
        DisplayBase::COL_SYS_NTSC_358,
        (void *)user_frame_buffer0,
        FRAME_BUFFER_STRIDE,
        DisplayBase::VIDEO_FORMAT_YCBCR422,
        DisplayBase::WR_RD_WRSWA_32_16BIT,
        VIDEO_PIXEL_VW,
        VIDEO_PIXEL_HW
    );
    EasyAttach_CameraStart(Display, DisplayBase::VIDEO_INPUT_CHANNEL_0);
}

#if MBED_CONF_APP_LCD
static void Start_LCD_Display(void) {
    DisplayBase::rect_t rect;

    rect.vs = 0;
    rect.vw = VIDEO_PIXEL_VW;
    rect.hs = 0;
    rect.hw = VIDEO_PIXEL_HW;
    Display.Graphics_Read_Setting(
        DisplayBase::GRAPHICS_LAYER_0,
        (void *)user_frame_buffer0,
        FRAME_BUFFER_STRIDE,
        DisplayBase::GRAPHICS_FORMAT_YCBCR422,
        DisplayBase::WR_RD_WRSWA_32_16BIT,
        &rect
    );
    Display.Graphics_Start(DisplayBase::GRAPHICS_LAYER_0);

    Thread::wait(50);
    EasyAttach_LcdBacklight(true);
}
#endif

#if (SAVA_FILE_TYPE == 0)
static void save_image_jpg(void) {
    size_t jcu_encode_size = 0;
    JPEG_Converter::bitmap_buff_info_t bitmap_buff_info;
    JPEG_Converter::encode_options_t   encode_options;

    bitmap_buff_info.width              = VIDEO_PIXEL_HW;
    bitmap_buff_info.height             = VIDEO_PIXEL_VW;
    bitmap_buff_info.format             = JPEG_Converter::WR_RD_YCbCr422;
    bitmap_buff_info.buffer_address     = (void *)user_frame_buffer0;

    encode_options.encode_buff_size     = sizeof(JpegBuffer);
    encode_options.p_EncodeCallBackFunc = NULL;
    encode_options.input_swapsetting    = JPEG_Converter::WR_RD_WRSWA_32_16_8BIT;

    dcache_invalid(JpegBuffer, sizeof(JpegBuffer));
    if (Jcu.encode(&bitmap_buff_info, JpegBuffer, &jcu_encode_size, &encode_options) == JPEG_Converter::JPEG_CONV_OK) {
        char file_name[32];
        sprintf(file_name, "/"MOUNT_NAME"/img_%d.jpg", file_name_index++);
        FILE * fp = fopen(file_name, "w");
        if (fp != NULL) {
            setvbuf(fp, NULL, _IONBF, 0); // unbuffered
            fwrite(JpegBuffer, sizeof(char), (int)jcu_encode_size, fp);
            fclose(fp);
        }
        printf("Saved file %s\r\n", file_name);
    }
}

int main() {
    EasyAttach_Init(Display);
    Start_Video_Camera();
#if MBED_CONF_APP_LCD
    Start_LCD_Display();
#endif
    SdUsbConnect storage(MOUNT_NAME);

    while (1) {
        storage.wait_connect();
        if (button0 == 0) {
            wait_new_image(); // wait for image input
            led1 = 1;
            save_image_jpg(); // save as jpeg
            led1 = 0;
        }
    }
}

#else

#define MAX_FRAME_NUM     1024
#define VIDEO_BUFF_SIZE  (VIDEO_PIXEL_HW * VIDEO_PIXEL_VW * 2)

static uint32_t mjpg_index[MAX_FRAME_NUM];
static uint32_t mjpg_size[MAX_FRAME_NUM];
static uint8_t work_buf[256];

static const uint8_t MJpegHeader[224] = {
    0x52, 0x49, 0x46, 0x46, // "RIFF"
    0xF0, 0xFF, 0xFF, 0x7F, // [Temporary] Total data size (File size - 8)
    0x41, 0x56, 0x49, 0x20, // "AVI "
    0x4C, 0x49, 0x53, 0x54, // "LIST"
    0xC0, 0x00, 0x00, 0x00, // Size of the list
    0x68, 0x64, 0x72, 0x6C, // "hdrl"
    0x61, 0x76, 0x69, 0x68, // "avih"
    0x38, 0x00, 0x00, 0x00, // avih chunk size
    0x35, 0x82, 0x00, 0x00, // [Temporary] Frame interval (microseconds)
    0x00, 0xCC, 0x00, 0x00, // Approximate maximum data rate
    0x00, 0x00, 0x00, 0x00, // Padding unit
    0x10, 0x00, 0x00, 0x00, // With index information
    0x00, 0x48, 0x00, 0x00, // [Temporary] Total number of frames
    0x00, 0x00, 0x00, 0x00, // dummy
    0x01, 0x00, 0x00, 0x00, // Number of streams
    0x00, 0x00, 0x10, 0x00, // Required buffer size (estimate)
    ((VIDEO_PIXEL_HW >> 0) & 0xFF), ((VIDEO_PIXEL_HW >> 8) & 0xFF), ((VIDEO_PIXEL_HW >> 16) & 0xFF), ((VIDEO_PIXEL_HW >> 24) & 0xFF), // width
    ((VIDEO_PIXEL_VW >> 0) & 0xFF), ((VIDEO_PIXEL_VW >> 8) & 0xFF), ((VIDEO_PIXEL_VW >> 16) & 0xFF), ((VIDEO_PIXEL_VW >> 24) & 0xFF), // height
    0x00, 0x00, 0x00, 0x00, // not use
    0x00, 0x00, 0x00, 0x00, // not use
    0x00, 0x00, 0x00, 0x00, // not use
    0x00, 0x00, 0x00, 0x00, // not use
    0x4C, 0x49, 0x53, 0x54, // "LIST"
    0x74, 0x00, 0x00, 0x00, // Size of the list
    0x73, 0x74, 0x72, 0x6C, // "strl"
    0x73, 0x74, 0x72, 0x68, // "strh"
    0x38, 0x00, 0x00, 0x00, // strl chunk size
    0x76, 0x69, 0x64, 0x73, // "vids"
    0x4D, 0x4A, 0x50, 0x47, // "MJPG"
    0x00, 0x00, 0x00, 0x00, // Stream handling is normal
    0x00, 0x00, 0x00, 0x00, // Stream priority 0, no language setting
    0x00, 0x00, 0x00, 0x00, // Audio first frame: None
    0x01, 0x00, 0x00, 0x00, // Number of frames per second (denominator)
    0x1E, 0x00, 0x00, 0x00, // [Temporary] Number of frames per second (numerator)
    0x00, 0x00, 0x00, 0x00, // Stream start size
    0x00, 0x48, 0x00, 0x00, // [Temporary] Stream length
    0x00, 0x00, 0x00, 0x00, // Buffer size: unknown
    0xFF, 0xFF, 0xFF, 0xFF, // Default quality
    0x00, 0x00, 0x00, 0x00, // Size per sample (change)
    0x00, 0x00, 0x00, 0x00, // Display coordinates (x, y in upper left)
    ((VIDEO_PIXEL_HW)& 0xFF), ((VIDEO_PIXEL_HW >> 8) & 0xFF), ((VIDEO_PIXEL_VW)& 0xFF), ((VIDEO_PIXEL_VW >> 8) & 0xFF), //[Temporary] Display coordinates (x, y in lower right)
    0x73, 0x74, 0x72, 0x66, // "strf"
    0x28, 0x00, 0x00, 0x00, // strf chunk size
    0x28, 0x00, 0x00, 0x00, // Chunk body size
    ((VIDEO_PIXEL_HW >> 0) & 0xFF), ((VIDEO_PIXEL_HW >> 8) & 0xFF), ((VIDEO_PIXEL_HW >> 16) & 0xFF), ((VIDEO_PIXEL_HW >> 24) & 0xFF), // width
    ((VIDEO_PIXEL_VW >> 0) & 0xFF), ((VIDEO_PIXEL_VW >> 8) & 0xFF), ((VIDEO_PIXEL_VW >> 16) & 0xFF), ((VIDEO_PIXEL_VW >> 24) & 0xFF), // height
    0x01, 0x00, 0x10, 0x00, // Number of faces and bpp
    0x4D, 0x4A, 0x50, 0x47, // "MJPG"
    ((VIDEO_BUFF_SIZE >> 0) & 0xFF), ((VIDEO_BUFF_SIZE >> 8) & 0xFF), ((VIDEO_BUFF_SIZE >> 16) & 0xFF), ((VIDEO_BUFF_SIZE >> 24) & 0xFF), // Buffer size (width x height x 2)
    0x00, 0x00, 0x00, 0x00, // Horizontal resolution
    0x00, 0x00, 0x00, 0x00, // Vertical resolution
    0x00, 0x00, 0x00, 0x00, // Number of color index
    0x00, 0x00, 0x00, 0x00, // Important color index number
    0x4C, 0x49, 0x53, 0x54, // "LIST"
    0xF0, 0xFF, 0xFF, 0x7F, // [Temporary] List size (index address - 0xDC)
    0x6D, 0x6F, 0x76, 0x69  // "movi"
};

static void set_data(uint8_t * buf, uint32_t data) {
    if (buf != NULL) {
        buf[0] = ((data >> 0)  & 0xFF);
        buf[1] = ((data >> 8)  & 0xFF);
        buf[2] = ((data >> 16) & 0xFF);
        buf[3] = ((data >> 24) & 0xFF);
    }
}

int main() {
    EasyAttach_Init(Display);
    Start_Video_Camera();
#if MBED_CONF_APP_LCD
    Start_LCD_Display();
#endif
    SdUsbConnect storage(MOUNT_NAME);
    FILE * fp;
    char file_name[32];
    int mjpg_pointer = 0;
    int mjpg_frame = 0;
    uint32_t fps;
    Timer t;

    while (1) {
        storage.wait_connect();

        if (led1 == 0) {
            if (button0 == 0) {
                led1 = 1;
                sprintf(file_name, "/"MOUNT_NAME"/movie_%d.avi", file_name_index++);
                fp = fopen(file_name, "w");
                if (fp != NULL) {
                    setvbuf(fp, NULL, _IONBF, 0); // unbuffered
                    mjpg_frame = 0;
                    mjpg_pointer = sizeof(MJpegHeader);
                    fseek(fp, mjpg_pointer, SEEK_SET);
                    t.reset();
                    t.start();
                } else {
                    led1 = 0;
                }
            }
        }
        if (led1 == 1) {
            if ((button0 == 0) && (mjpg_frame < MAX_FRAME_NUM)) {
                size_t jcu_encode_size = 0;
                JPEG_Converter::bitmap_buff_info_t bitmap_buff_info;
                JPEG_Converter::encode_options_t   encode_options;

                wait_new_image(); // wait for image input

                bitmap_buff_info.width              = VIDEO_PIXEL_HW;
                bitmap_buff_info.height             = VIDEO_PIXEL_VW;
                bitmap_buff_info.format             = JPEG_Converter::WR_RD_YCbCr422;
                bitmap_buff_info.buffer_address     = (void *)user_frame_buffer0;

                encode_options.encode_buff_size     = sizeof(JpegBuffer);
                encode_options.p_EncodeCallBackFunc = NULL;
                encode_options.input_swapsetting    = JPEG_Converter::WR_RD_WRSWA_32_16_8BIT;

                dcache_invalid(JpegBuffer, sizeof(JpegBuffer));
                if (Jcu.encode(&bitmap_buff_info, JpegBuffer, &jcu_encode_size, &encode_options) == JPEG_Converter::JPEG_CONV_OK) {
                    if ((jcu_encode_size & 0x1) != 0) {
                        JpegBuffer[jcu_encode_size] = 0;
                        jcu_encode_size++;
                    }
                    memcpy(&work_buf[0], "00dc", 4);
                    set_data(&work_buf[4], jcu_encode_size);
                    fwrite(work_buf, sizeof(char), (int)8, fp);
                    fwrite(JpegBuffer, sizeof(char), (int)jcu_encode_size, fp);
                    mjpg_index[mjpg_frame] = mjpg_pointer + 4 - sizeof(MJpegHeader);
                    mjpg_size[mjpg_frame] = jcu_encode_size;
                    mjpg_frame++;
                    mjpg_pointer += (jcu_encode_size + 8);
                }
            } else {
                t.stop();
                fps = mjpg_frame * 1000 / t.read_ms();

                memcpy(work_buf, MJpegHeader, sizeof(MJpegHeader));
                set_data(&work_buf[4], mjpg_pointer + 16 + (mjpg_frame * 8) - 8);
                set_data(&work_buf[32], 1000000.0 / fps);
                set_data(&work_buf[48], mjpg_frame);
                set_data(&work_buf[132], fps);
                set_data(&work_buf[140], mjpg_frame);
                if (mjpg_pointer > 0xDC){
                    set_data(&work_buf[216], mjpg_pointer - 0xDC);
                } else {
                    set_data(&work_buf[216],0);
                }
                fseek(fp, 0, SEEK_SET);
                fwrite(work_buf, sizeof(char), sizeof(MJpegHeader), fp);

                memcpy(&work_buf[0], "idx1", 4);
                set_data(&work_buf[4], mjpg_frame * 16);
                fseek(fp, mjpg_pointer, SEEK_SET);
                fwrite(work_buf, sizeof(char), 8, fp);

                for (int i = 0; i < mjpg_frame; i++){
                    memcpy(&work_buf[0], "00dc", 4);
                    set_data(&work_buf[4], 16);
                    set_data(&work_buf[8], mjpg_index[i]);
                    set_data(&work_buf[12], mjpg_size[i]);
                    fwrite(work_buf, sizeof(char), 16, fp);
                }

                fclose(fp);
                printf("Saved file %s , %dfps\r\n", file_name, fps);
                led1 = 0;
            }
        }
    }
}
#endif
