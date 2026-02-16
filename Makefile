
EXAMPLES = examples/basic examples/3dtv

all:
	@for dir in $(EXAMPLES); do $(MAKE) -C $$dir; done

clean:
	@for dir in $(EXAMPLES); do $(MAKE) -C $$dir clean; done

run-basic:
	$(MAKE) -C examples/basic run

run-3dtv:
	$(MAKE) -C examples/3dtv run

dist:
	@for dir in $(EXAMPLES); do $(MAKE) -C $$dir dist; done
