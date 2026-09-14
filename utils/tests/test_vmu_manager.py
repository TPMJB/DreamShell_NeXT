from pathlib import Path
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as E

ROOT=Path(__file__).resolve().parents[2]

class VMUManagerTests(unittest.TestCase):
    def test_transfer_failure_and_confirmation_gates(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe=Path(tmp)/'vmu-transfer'
            subprocess.run(['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-fno-pie','-no-pie','utils/tests/vmu_transfer_harness.c','-o',str(exe)],cwd=ROOT,check=True)
            self.assertIn('passed',subprocess.check_output([str(exe)],text=True))

    def test_layout_exports_and_resources(self):
        app=ROOT/'applications/vmu_manager'
        tree=E.parse(app/'app.xml');root=tree.getroot()
        exports=set((app/'modules/exports.txt').read_text().splitlines())
        names=[e.get('name') for e in root.iter() if e.get('name')]
        # Widget and resource names may overlap (e.g. progressbar).
        widget_names=[e.get('name') for e in root.find('body').iter() if e.get('name')]
        self.assertEqual(len(widget_names),len(set(widget_names)))
        for e in root.iter():
            for attr,value in e.attrib.items():
                if value.startswith('export:'):self.assertIn(value[7:].split('(')[0],exports)
        self.assertEqual(root.get('version'),'2.0.0')
        for slot in ['A1','A2','B1','B2','C1','C2','D1','D2']:self.assertIn(slot,names)
        for control in ['copy-button','dump-button','delete-button','format-c','modal-cancel','modal-accept']:self.assertIn(control,names)

    def test_cancel_folder_does_not_call_mkdir(self):
        # Compile the production handler against a minimal UI/storage harness.
        source=(ROOT/'applications/vmu_manager/modules/module.c').read_text()
        body=source.split('void VMU_Manager_make_folder(GUI_Widget *widget) {',1)[1].split('\nvoid VMU_Manager_clr_name',1)[0]
        stub='''#include <stdio.h>
#include <string.h>
#include <assert.h>
#define NAME_MAX 256
typedef int GUI_Widget;
static struct { GUI_Widget *pages,*filebrowser2,*folder_name; } self;
static int made;
static const char *GUI_ObjectGetName(GUI_Widget *w) {(void)w;return "confirm-no";}
static void GUI_CardStackShowIndex(GUI_Widget *w,int p) {(void)w;(void)p;}
static const char *GUI_FileManagerGetPath(GUI_Widget *w) {(void)w;return "/sd";}
static const char *GUI_TextEntryGetText(GUI_Widget *w) {(void)w;return "new_folder";}
static void GUI_FileManagerScan(GUI_Widget *w) {(void)w;}
static void ui_status(const char *s) {(void)s;}
static int fs_mkdir(const char *s) {(void)s;++made;return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c=Path(tmp)/'cancel.c';exe=Path(tmp)/'cancel'
            c.write_text(stub+'\n#include "'+str(ROOT/'applications/vmu_manager/modules/ui_logic.h')+'"\nvoid VMU_Manager_make_folder(GUI_Widget *widget) {'+body+'\nint main(void) { VMU_Manager_make_folder(0); assert(!made); return 0;}\n')
            subprocess.run(['gcc','-std=c11',str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
