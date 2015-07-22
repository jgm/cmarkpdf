cmarkpdf
========

This is an experimental native PDF renderer for
[cmark](https://github.com/jgm/cmark), using
[libharu](https://github.com/libharu/libharu).

It links dynamically against both libcmark and
libhpdf (libharu), which must be installed.
A recent version of libhpdf that supports UTF-8
encoding is needed.

To build on Linux or OSX, `make`.

To use:

    ./cmarkpdf --smart -o output.pdf input.txt

Note that paths to fonts are hardcoded and may need
to be adjusted if your system puts fonts in a different
place or has different fonts.

Roadmap
-------

- [x] Line wrapping and justification, using greedy
      algorithm.  (In the future we might explore using
      Knuth-Prass.)
- [x] List items (currently all treated as bulleted).
- [ ] Proper list markers for ordered and bullet lists
      (bullet lists should use different bullets at different
      indent level).
- [x] Code spans
- [x] Code blocks
- [x] Block quotes
- [x] Hrules
- [x] Headers
- [ ] Improve block quotes with smaller font
- [ ] Links
- [ ] Images
- [ ] Strong
- [ ] Emph
- [ ] Better error handling (e.g. when a font file is missing)
- [ ] Customizability (e.g. selecting fonts, margins, sizes)
- [ ] Ensure that PDF is searchable and copy-pasteable.
      Currently the PDFs work well is some viewers (e.g. Chrome's or
      Adobe Reader) but not in others.  In Ubuntu Evince the text is not
      selectable.  In OSX Mavericks Preview, the text can be selected
      and copied, but it is garbled when pasted.  See
      https://groups.google.com/forum/#!searchin/libharu/copy$20paste/libharu/YzXoH_K3OAI/YNCsn6XXF-gJ,
      http://superuser.com/questions/137824/pdf-has-garbled-text-when-copy-pasting,
      and comments in the code.
