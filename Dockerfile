FROM wiiuenv/devkitppc:20220806

COPY --from=wiiuenv/wiiupluginsystem:20220724 /artifacts $DEVKITPRO
COPY --from=wiiuenv/libmocha:20220827 /artifacts $DEVKITPRO

WORKDIR project