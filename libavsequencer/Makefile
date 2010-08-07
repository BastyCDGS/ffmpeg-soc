include $(SUBDIR)../config.mak

NAME = avsequencer
FFLIBS = avcodec avformat avutil

HEADERS = avsequencer.h \
          instr.h       \
          module.h      \
          order.h       \
          player.h      \
          sample.h      \
          song.h        \
          synth.h       \
          track.h       \

OBJS = allmixers.o      \
       avsequencer.o    \
       instr.o          \
       module.o         \
       order.o          \
       player.o         \
       sample.o         \
       song.o           \
       synth.o          \
       track.o          \

OBJS-$(CONFIG_LOW_QUALITY_MIXER)    += lq_mixer.o
OBJS-$(CONFIG_NULL_MIXER)           += null_mixer.o

include $(SUBDIR)../subdir.mak
