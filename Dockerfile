FROM wiiuenv/devkitppc:20200625

COPY --from=wiiuenv/wiiupluginsystem:20200626 /artifacts $DEVKITPRO
COPY --from=wiiuenv/libiosuhax:20200627 /artifacts $DEVKITPRO

WORKDIR project