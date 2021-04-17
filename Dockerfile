FROM wiiuenv/devkitppc:20210414

COPY --from=wiiuenv/wiiupluginsystem:20210417 /artifacts $DEVKITPRO
COPY --from=wiiuenv/libiosuhax:20210109 /artifacts $DEVKITPRO

WORKDIR project