NAME = d_drumcloud
FILES_DSP = SendNoteExamplePlugin.cpp AudioFileLoader.cpp PitchDetector.cpp FilteredStereoDelay.cpp
FILES_UI = DrumCloudUI.cpp AudioFileLoader.cpp ExternalUrl.cpp
UI_TYPE = opengl
CXXFLAGS += -std=gnu++17
DPF_BUILD_DIR = $(CURDIR)/build/d_drumcloud
DPF_TARGET_DIR = $(CURDIR)/bin
TARGETS ?= clap vst3

# Opt in explicitly because AU is only available on macOS. This single-token
# switch is safe to pass through CI actions and keeps local Linux builds intact.
ifeq ($(DRUMCLOUD_BUILD_AU),true)
TARGETS += au
endif

include DPF/Makefile.plugins.mk

all: $(TARGETS)
