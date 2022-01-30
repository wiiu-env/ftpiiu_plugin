FROM wiiuenv/devkitppc:20211229

COPY --from=wiiuenv/wiiupluginsystem:20220123 /artifacts $DEVKITPRO
COPY --from=wiiuenv/libiosuhax:20220129 /artifacts $DEVKITPRO

WORKDIR project