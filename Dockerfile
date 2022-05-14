FROM wiiuenv/devkitppc:20220507

COPY --from=wiiuenv/wiiupluginsystem:20220513 /artifacts $DEVKITPRO
COPY --from=wiiuenv/libiosuhax:20220129 /artifacts $DEVKITPRO

WORKDIR project