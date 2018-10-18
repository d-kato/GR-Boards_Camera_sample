# GR-Boads_Camera_sample
GR-PEACH、および、GR-LYCHEEで動作するサンプルプログラムです。  
GR-LYCHEEの開発環境については、[GR-LYCHEE用オフライン開発環境の手順](https://developer.mbed.org/users/dkato/notebook/offline-development-lychee-langja/)を参照ください。


## 概要
カメラ画像をUSB、または、SDに保存するサンプルです。  
USBとSDが両方挿入されている場合は、先に検出した方のデバイスに接続します。  
``USER_BUTTON0``を押すとJPEG形式で保存します。  
下記の設定を変更することで、AVI形式の動画ファイルとして保存することもできます。  
```cpp
/**** User Selection *********/
#define SAVA_FILE_TYPE         (0)     /* Select  0(Image(.jpg)) or 1(Movie(.avi)) */
/*****************************/
```
JPEG変換には [JCU](https://developer.mbed.org/teams/Renesas/code/GraphicsFramework/) を使用しています。  

カメラとLCDの指定を行う場合は``mbed_app.json``に``camera-type``と``lcd-type``を追加してください。  
詳細は``mbed-gr-libs/README.md``を参照ください。  
```json
{
    "config": {
        "camera":{
            "help": "0:disable 1:enable",
            "value": "1"
        },
        "camera-type":{
            "help": "Please see mbed-gr-libs/README.md",
            "value": "CAMERA_CVBS"
        },
        "lcd":{
            "help": "0:disable 1:enable",
            "value": "0"
        },
        "lcd-type":{
            "help": "Please see mbed-gr-libs/README.md",
            "value": "GR_PEACH_4_3INCH_SHIELD"
        },
        "usb-host-ch":{
            "help": "(for GR-PEACH) 0:ch0 1:ch1",
            "value": "1"
        },
        "audio-camera-shield":{
            "help": "(for GR-PEACH) 0:not use 1:use",
            "value": "1"
        }
    },
    "target_overrides": {
        "*": {
            "target.macros_add": ["HAVE_OPENCV_IMGCODECS"]
        }
    }
}
```
***Mbed CLI以外の環境で使用する場合***  
Mbed以外の環境(CLI or Mbedオンラインコンパイラ 以外の環境)をお使いの場合、``mbed_app.json``の変更は反映されません。  
``mbed_config.h``に以下のようにマクロを追加してください。  
```cpp
#define MBED_CONF_APP_CAMERA                        1    // set by application
#define MBED_CONF_APP_CAMERA_TYPE                   CAMERA_CVBS             // set by application
#define MBED_CONF_APP_LCD                           0    // set by application
#define MBED_CONF_APP_USB_HOST_CH                   1    // set by application
#define MBED_CONF_APP_AUDIO_CAMERA_SHIELD           1    // set by application
#define HAVE_OPENCV_IMGCODECS
```

カメラ画像をLCDやWindows用PCアプリで表示する場合は [GR-Boads_Camera_LCD_sample](https://github.com/d-kato/GR-Boads_Camera_LCD_sample) を参照ください。  
