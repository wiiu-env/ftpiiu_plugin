FROM wiiuenv/devkitppc:20220917

COPY --from=wiiuenv/wiiupluginsystem:20220918 /artifacts $DEVKITPRO
COPY --from=wiiuenv/libmocha:20220919 /artifacts $DEVKITPRO

WORKDIR project