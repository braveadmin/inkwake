SCAD   ?= openscad
SRC    := inkwake.scad
PARTS  := front back
OUT    := build

all: $(PARTS:%=$(OUT)/%.stl)

$(OUT)/%.stl: $(SRC) | $(OUT)
	$(SCAD) -q -D 'part="$*"' -o $@ $<

$(OUT):
	mkdir -p $@

clean:
	rm -rf $(OUT)

.PHONY: all clean
