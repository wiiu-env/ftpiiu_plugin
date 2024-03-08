[![CI-Release](https://github.com/wiiu-env/ftpiiu_plugin/actions/workflows/ci.yml/badge.svg)](https://github.com/wiiu-env/ftpiiu_plugin/actions/workflows/ci.yml)

# ftpiiu - A ftp server plugin for the Wii U based on ftpd

## Installation
(`[ENVIRONMENT]` is a placeholder for the actual environment name.)

1. Copy the file `ftpiiu.wps` into `sd:/wiiu/environments/[ENVIRONMENT]/plugins`.
2. Requires the [WiiUPluginLoaderBackend](https://github.com/wiiu-env/WiiUPluginLoaderBackend) in `sd:/wiiu/environments/[ENVIRONMENT]/modules`.

## Usage information and settings

- By default, the FTPiiU server is running as long the plugin loaded (file is in the plugin directory of your environment).
- Access to the system files is **disabled by default**, you can enable it in the config menu.
- To connect to the server you can use empty credentials
- The SD card can be accessed via `/fs/vol/external01/`

Via the plugin config menu (press L, DPAD Down and Minus on the gamepad) you can configure the plugin. The available options are the following:
- **Settings**:
  - Enable FTPiiU:
    - Starts/Stops the ftp server which is running in the background. Changes take effect when so close the config menu. (Default is true).
  - Allow access to system files:
    - Allows you to access all system files. If this option is disabled, you can only access `/fs/vol/content`, `/fs/vol/save` and `/fs/vol/external01` (SD card). Changes take effect when so close the config menu, but the server may restart. (Default is false).
- Additionally, the config menu will display the IP of your console and the port the server is running at.

See the [ftpd repository](https://github.com/mtheall/ftpd?tab=readme-ov-file#supported-commands) for a list of all supported commands.

## Buildflags

### Logging
Building via `make` only logs errors (via OSReport). To enable logging via the [LoggingModule](https://github.com/wiiu-env/LoggingModule) set `DEBUG` to `1` or `VERBOSE`.

`make` Logs errors only (via OSReport).  
`make DEBUG=1` Enables information and error logging via [LoggingModule](https://github.com/wiiu-env/LoggingModule).  
`make DEBUG=VERBOSE` Enables verbose information and error logging via [LoggingModule](https://github.com/wiiu-env/LoggingModule).

If the [LoggingModule](https://github.com/wiiu-env/LoggingModule) is not present, it'll fallback to UDP (Port 4405) and [CafeOS](https://github.com/wiiu-env/USBSerialLoggingModule) logging.

## Building using the Dockerfile

It's possible to use a docker image for building. This way you don't need anything installed on your host system.

```
# Build docker image (only needed once)
docker build . -t ftpiiuplugin-builder

# make 
docker run -it --rm -v ${PWD}:/project ftpiiuplugin-builder make

# make clean
docker run -it --rm -v ${PWD}:/project ftpiiuplugin-builder make clean
```

## Format the code via docker

`docker run --rm -v ${PWD}:/src ghcr.io/wiiu-env/clang-format:13.0.0-2 -r ./src -i`

## Credits

This plugin is based on [ftpd](https://github.com/mtheall/ftpd) by mtheall