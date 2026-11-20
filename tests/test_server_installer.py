"""Run the actual installer in Windows PowerShell 5.1 on disposable map folders."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from native_test_support import ROOT


class ServerInstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp = Path(tempfile.mkdtemp(prefix='dragon-server-installer-'))
        self.project = self.temp / 'project'
        self.bundle = self.temp / 'package'
        self.bundle.mkdir()
        self.code = b'new-server-binary-fixture'
        (self.bundle/'MapServer.exe').write_bytes(self.code)
        (self.bundle/'server-update.json').write_text(json.dumps({
            'version':'1.0.1','sha256':hashlib.sha256(self.code).hexdigest()
        }))
        shutil.copyfile(ROOT/'tools/install-family-server.ps1', self.bundle/'install.ps1')
        for name in ('map-a','map-z'):
            folder = self.project/'local-server'/name
            folder.mkdir(parents=True)
            (folder/'MapServer.exe').write_bytes(name.encode())
            (folder/'MapServer.ini').write_text('preserve-configuration')

    def run_installer(self):
        env = dict(os.environ)
        # PowerShell 7's module path cannot be inherited by Windows PowerShell 5.
        for key in list(env):
            if key.upper() == 'PSMODULEPATH': env.pop(key)
        return subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass',
                               '-File',str(self.bundle/'install.ps1'),'-Projeto',str(self.project)],
                              capture_output=True,text=True,env=env,timeout=40)

    def test_update_all_maps_preserves_config_and_original_backups(self):
        result = self.run_installer()
        self.assertEqual(result.returncode,0,result.stderr)
        for name in ('map-a','map-z'):
            folder = self.project/'local-server'/name
            self.assertEqual((folder/'MapServer.exe').read_bytes(),self.code)
            self.assertEqual((folder/'MapServer.ini').read_text(),'preserve-configuration')
            backups = list((self.project/'local-server/_backup_atualizacoes').glob('*/'+name+'/MapServer.exe'))
            self.assertEqual(len(backups),1)
            self.assertEqual(backups[0].read_bytes(),name.encode())

    def test_locked_second_map_restores_first_map(self):
        # Windows denies atomic replacement while this handle lacks delete sharing.
        with (self.project/'local-server/map-z/MapServer.exe').open('rb'):
            result = self.run_installer()
        self.assertNotEqual(result.returncode,0)
        for name in ('map-a','map-z'):
            self.assertEqual((self.project/'local-server'/name/'MapServer.exe').read_bytes(),name.encode())

    def add_database_component(self):
        database = self.project/'local-server/db'
        database.mkdir()
        (database/'RajaDB.exe').write_bytes(b'old-database')
        new_database = b'new-database-binary-fixture'
        (self.bundle/'RajaDB.exe').write_bytes(new_database)
        (self.bundle/'server-update.json').write_text(json.dumps({
            'version':'1.1.0', 'files':[
                {'name':'MapServer.exe','sha256':hashlib.sha256(self.code).hexdigest()},
                {'name':'RajaDB.exe','sha256':hashlib.sha256(new_database).hexdigest()}
            ]
        }))
        return database, new_database

    def test_database_and_maps_update_together(self):
        database, new_database = self.add_database_component()
        result = self.run_installer()
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual((database/'RajaDB.exe').read_bytes(),new_database)
        for name in ('map-a','map-z'):
            self.assertEqual((self.project/'local-server'/name/'MapServer.exe').read_bytes(),self.code)

    def test_locked_map_rolls_back_database_too(self):
        database, _ = self.add_database_component()
        with (self.project/'local-server/map-z/MapServer.exe').open('rb'):
            result = self.run_installer()
        self.assertNotEqual(result.returncode,0)
        self.assertTrue(list((self.project/'local-server/_backup_atualizacoes').glob('*/db/RajaDB.exe')))
        self.assertEqual((database/'RajaDB.exe').read_bytes(),b'old-database')
        for name in ('map-a','map-z'):
            self.assertEqual((self.project/'local-server'/name/'MapServer.exe').read_bytes(),name.encode())
