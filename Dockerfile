FROM ghcr.io/wiiu-env/devkitppc:20240423

COPY --from=ghcr.io/wiiu-env/wiiupluginsystem:20240425 /artifacts $DEVKITPRO
COPY --from=ghcr.io/wiiu-env/libmocha:20231127 /artifacts $DEVKITPRO

WORKDIR project