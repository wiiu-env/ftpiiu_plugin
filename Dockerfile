FROM ghcr.io/wiiu-env/devkitppc:20220917

COPY --from=ghcr.io/wiiu-env/wiiupluginsystem:20220918 /artifacts $DEVKITPRO
COPY --from=ghcr.io/wiiu-env/libmocha:20220919 /artifacts $DEVKITPRO

WORKDIR project