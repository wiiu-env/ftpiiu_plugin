FROM wiiuenv/devkitppc:20210101

COPY --from=wiiuenv/wiiupluginsystem:20210316 /artifacts $DEVKITPRO
COPY --from=wiiuenv/libiosuhax:20210109 /artifacts $DEVKITPRO

WORKDIR project