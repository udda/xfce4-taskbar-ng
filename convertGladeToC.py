#!/usr/bin/python

import xml.dom.minidom

header = """#ifdef __SUNPRO_C
#pragma align 4 (taskbar_dialog_ui)
#endif
#ifdef __GNUC__
static const char taskbar_dialog_ui[] __attribute__ ((__aligned__ (4))) =
#else
static const char taskbar_dialog_ui[] =
#endif
{
"""

footer = """};
static const unsigned taskbar_dialog_ui_length = %iu;
"""

class XmlWriter:
    def __init__(self):
        self.snippets = []
    def write(self, data):
        if data.isspace(): return
        self.snippets.append(data)
    def __str__(self):
        return ''.join(self.snippets)

if __name__ == "__main__":
    writer = XmlWriter()
    xml = xml.dom.minidom.parse("taskbar-dialog.glade")
    xml.writexml(writer)
    strippedXml = ("%s" % (writer)).replace('"', '\\"')
    
    byteCount = len(strippedXml)
    baseOffset=0
    stripSize=64
    
    output = open("taskbar-dialog_ui.h", 'w')
    output.write(header)
    
    while baseOffset < byteCount:
        skipTrailingQuote = 0
        if baseOffset+stripSize < byteCount and strippedXml[baseOffset+stripSize] == '"':
            skipTrailingQuote = 1
        output.write('  "%s"\n' % (strippedXml[baseOffset:baseOffset+stripSize+skipTrailingQuote]))
        baseOffset += stripSize + skipTrailingQuote
    
    output.write(footer % (byteCount))
    output.close()
    
