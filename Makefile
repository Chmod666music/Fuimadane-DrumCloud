NAME = d_drumcloud
FILES_DSP = SendNoteExamplePlugin.cpp AudioFileLoader.cpp PitchDetector.cpp FilteredStereoDelay.cpp
FILES_UI = DrumCloudUI.cpp AudioFileLoader.cpp
UI_TYPE = opengl
CXXFLAGS += -std=gnu++17
DPF_BUILD_DIR = $(CURDIR)/build/d_drumcloud
DPF_TARGET_DIR = $(CURDIR)/bin
TARGETS ?= clap vst3

include DPF/Makefile.plugins.mk

all: $(TARGETS)
