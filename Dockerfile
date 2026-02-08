FROM ghcr.io/wiiu-env/devkitppc:20260204

COPY --from=ghcr.io/wiiu-env/wiiupluginsystem:20260208 /artifacts $DEVKITPRO
COPY --from=ghcr.io/wiiu-env/libmocha:20260126 /artifacts $DEVKITPRO

WORKDIR /project