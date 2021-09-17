FROM wiiuenv/devkitppc:20210917

COPY --from=wiiuenv/wiiupluginsystem:20210917 /artifacts $DEVKITPRO
COPY --from=wiiuenv/libiosuhax:20210109 /artifacts $DEVKITPRO

WORKDIR project