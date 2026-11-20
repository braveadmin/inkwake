SCAD   ?= openscad
SRC    := inkwake.scad
PARTS  := front back
OUT    := build

all: $(PARTS:%=$(OUT)/%.stl) $(OUT)/touch_test.stl

$(OUT)/%.stl: $(SRC) | $(OUT)
	$(SCAD) -q -D 'part="$*"' -o $@ $<

$(OUT)/touch_test.stl: touch_test.scad | $(OUT)
	$(SCAD) -q -o $@ $<

$(OUT):
	mkdir -p $@

clean:
	rm -rf $(OUT)

.PHONY: all clean
