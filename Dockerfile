FROM ghcr.io/wiiu-env/devkitppc:20241128

COPY --from=ghcr.io/wiiu-env/wiiupluginsystem:0.8.2-dev-20250125-f8faa9f /artifacts $DEVKITPRO
COPY --from=ghcr.io/wiiu-env/libmocha:20240603  /artifacts $DEVKITPRO

WORKDIR project