PROGRAM_EXCEPT_XINSTALL=\
basename \
cat \
chkblayout \
chmod \
chroot \
chvideomode \
clear \
colormake \
column \
command-not-found \
cp \
date \
df \
dirname \
du \
echo \
env \
expr \
false \
find \
head \
help \
id \
kernelinfo \
kill \
ln \
ls \
memstat \
mkdir \
mktemp \
mv \
pager \
passwd \
ps \
pstree \
pwd \
realpath \
rm \
rmdir \
sleep \
sort \
stat \
tail \
time \
touch \
tr \
true \
type \
uname \
uniq \
unmount \
uptime \
wc \
which \
xinstall \
yes \

PROGRAM=$(PROGRAM_EXCEPT_XINSTALL) xinstall

.include <sortix/sys.mk>
.include <template/dependencies.mk>
.include <template/program.mk>

install-bin: $(PROGRAM)
	mkdir -p $(DESTDIR)$(BINDIR)
.for program in $(PROGRAM_EXCEPT_XINSTALL)
	install $(program) $(DESTDIR)$(BINDIR)
.endfor
	install xinstall $(DESTDIR)$(BINDIR)/install
