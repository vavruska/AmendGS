CC=occ
_OBJ= main.a repo.a util.a bile.a browser.a focusable.a strnatcmp.a committer.a commit_list.a diffreg.a settings.a editor.a patch.a \
           visualize.a characters.root
OBJ=$(patsubst %,$(ODIR)/%,$(_OBJ))
ODIR=o
DEPS=AmendGS.h

all: $(ODIR)/._AmendGS.r AmendGS

AmendGS: $(OBJ)
	$(CC) -o $@ $(OBJ)
	iix chtyp -t S16 -a 0xDB03 $@

$(ODIR)/%.a: %.c $(DEPS)
	@mkdir -p o
	$(CC) --lint=-1 -F -O -1 -c -o $@ $< 

$(ODIR)/._AmendGS.r:  AmendGS.rez AmendGS.equ
	@mkdir -p o
	occ -o $(ODIR)/AmendGS.r AmendGS.rez
	cp $(ODIR)/._AmendGS.r ._AmendGS

$(ODIR)/%.root: %.asm
	@mkdir -p o
	$(CC) -c -o $@ $< 

$(ODIR)/main.a: main.c AmendGSRez.h AmendGS.h browser.h repo.h

$(ODIR)/repo.a: repo.c repo.h bile.h diff.h diff.h util.h strnatcmp.h

$(ODIR)/util.a: util.c util.h

$(ODIR)/browser.a: browser.c browser.h bile.h committer.h diff.h focusable.h repo.h visualize.h

$(ODIR)/bile.a: bile.c bile.h util.h

$(ODIR)/settings.a: settings.c settings.h util.h

$(ODIR)/committer..a: committer..c committer..h util.h  browser.h bile.h diff.h focusable.h repo.h

$(ODIR)/commit_list.a: commit_list.c committer.h browser.h repo.h util.h

$(ODIR)/editor.a: editor.c editor.h browser.h repo.h util.h focusable.h

$(ODIR)/patch.a: patch.c patch.h repo.h bile.h util.h 

$(ODIR)/visualize.a: visualize.c visualize.h repo.h bile.h browser.h util.h 

clean:
	@rm -f $(ODIR)/*.a $(ODIR)/*.root AmendGS $(ODIR)/AmendGS.r $(ODIR)/._AmendGS.r
	@rm -f AmendGS $(ODIR)/characters.*
	@rm -f ._AmendGS
