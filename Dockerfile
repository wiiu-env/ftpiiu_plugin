FROM ghcr.io/wiiu-env/devkitppc:20231112

COPY --from=ghcr.io/wiiu-env/wiiupluginsystem:20230719 /artifacts $DEVKITPRO
COPY --from=ghcr.io/wiiu-env/libmocha:20230621 /artifacts $DEVKITPRO

WORKDIR project