#!/usr/bin/env python3
"""Tests port_game_data.py: the SQL it writes, on a small database shaped like the seed's.

usage: python3 -m unittest port/db/test_port_game_data.py  (CTest: PortGameDataTests)
"""

import csv
import os
import sqlite3
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import port_game_data  # noqa: E402

HIGH_UPGRADE, LOW_UPGRADE, MIDDLE_UPGRADE = 379016000, 379221000, 379205000


def write_table(path, rows, columns):
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f, quoting=csv.QUOTE_ALL)
        writer.writerow([f"{i + 1} - Int32" for i in range(columns)])
        for row in rows:
            writer.writerow(row)


def org_row(item_id, ext_index, name, price=0, grade=0):
    row = ["0"] * 37
    row[0], row[1], row[2], row[20], row[36] = str(item_id), str(ext_index), name, str(price), str(grade)
    return row


def ext_row(ext_id, attrib, soul_bind=0):
    row = ["0"] * 47
    row[0], row[7], row[29] = str(ext_id), str(attrib), str(soul_bind)
    return row


class PortGameDataTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        data = self.dir.name

        # A dagger (grade 1) and a kukri (grade 3) sharing ext table 0, a shield in table 1 with no
        # grade, and the scrolls.
        write_table(os.path.join(data, "Item_Org_us.tbl.csv"), [
            org_row(110110000, 0, "Dagger", grade=1),
            org_row(110910000, 0, "Kukri", grade=3),
            org_row(170110000, 1, "Small Shield", grade=0),
            org_row(HIGH_UPGRADE, 22, "Upgrade Scroll (High Class Item)", 240000),
            org_row(LOW_UPGRADE, 22, "Upgrade Scroll (Low Class item)", 10000),
            org_row(MIDDLE_UPGRADE, 22, "Upgrade Scroll (Middle Class item)", 100000),
        ], 37)
        write_table(os.path.join(data, "Item_Ext_0_us.tbl.csv"), [
            ext_row(1, 5),                 # (+1): upgrade
            ext_row(7, 5, soul_bind=1),    # (+7): one class higher
            ext_row(201, 2),               # rare: no class
        ], 47)
        write_table(os.path.join(data, "Item_Ext_1_us.tbl.csv"), [ext_row(501, 5)], 47)

        self.db = sqlite3.connect(":memory:")
        self.db.create_collation("KO_CI", lambda a, b: (a.lower() > b.lower()) - (a.lower() < b.lower()))
        self.db.executescript(f"""
            CREATE TABLE ITEM (
              Num int NOT NULL, strName varchar(50) COLLATE KO_CI NOT NULL, Kind tinyint NOT NULL,
              BuyPrice int NOT NULL, SellPrice int NOT NULL, Countable tinyint NOT NULL,
              SellingGroup int NOT NULL, PRIMARY KEY (Num)) WITHOUT ROWID;
            INSERT INTO ITEM VALUES ({HIGH_UPGRADE}, 'Upgrade Scroll (+0)', 98, 240000, 0, 1, 254);
            INSERT INTO ITEM VALUES (110110001, 'Dagger (+1)', 11, 1000, 0, 0, 201);

            CREATE TABLE ITEM_UPGRADE (
              nIndex int NOT NULL, nNPCNum smallint NOT NULL, strName varchar(50) COLLATE KO_CI,
              strNote varchar(100) COLLATE KO_CI, nOriginType smallint NOT NULL, nOriginItem smallint NOT NULL,
              nReqItem1 int NOT NULL, nReqItem2 int NOT NULL, nReqItem3 int NOT NULL, nReqItem4 int NOT NULL,
              nReqItem5 int NOT NULL, nReqItem6 int NOT NULL, nReqItem7 int NOT NULL, nReqItem8 int NOT NULL,
              nReqNoah int NOT NULL, bRateType tinyint NOT NULL, nGenRate smallint NOT NULL,
              nGiveItem smallint NOT NULL, PRIMARY KEY (nIndex)) WITHOUT ROWID;
            INSERT INTO ITEM_UPGRADE VALUES (120001, 0, 'up', '', -1, 1, {HIGH_UPGRADE}, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9500, 1);
            INSERT INTO ITEM_UPGRADE VALUES (120002, 0, 'up', '', -1, 2, 700002000, {HIGH_UPGRADE}, 0, 0, 0, 0, 0, 0, 0, 0, 5000, 1);
            INSERT INTO ITEM_UPGRADE VALUES (120003, 0, 'blessed', '', -1, 1, 379021000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3000, 1);
        """)

    def tearDown(self):
        self.db.close()
        self.dir.cleanup()

    def apply(self):
        self.db.executescript(port_game_data.build_sql(self.dir.name))

    def test_item_grades_from_client_tables(self):
        self.assertEqual(port_game_data.item_grades(self.dir.name), {
            110110001: 1,
            110110007: 2,   # grade 1 + soul bind 1
            110910001: 3,
            110910007: 3,   # capped at high
        })

    def test_grades_table(self):
        self.apply()
        self.assertEqual(sorted(self.db.execute("SELECT Num, Grade FROM ITEM_GRADE")),
                         [(110110001, 1), (110110007, 2), (110910001, 3), (110910007, 3)])

    def test_scrolls_copy_their_high_class_counterpart(self):
        self.apply()
        rows = {r[0]: r[1:] for r in self.db.execute(
            "SELECT Num, strName, Kind, BuyPrice, SellPrice, Countable, SellingGroup FROM ITEM")}
        self.assertEqual(rows[LOW_UPGRADE], ("Upgrade Scroll (Low Class item)", 98, 10000, 0, 1, 254))
        self.assertEqual(rows[MIDDLE_UPGRADE], ("Upgrade Scroll (Middle Class item)", 98, 100000, 0, 1, 254))
        self.assertEqual(rows[HIGH_UPGRADE], ("Upgrade Scroll (+0)", 98, 240000, 0, 1, 254))

    def test_recipes_copied_for_each_class(self):
        self.apply()
        rows = {r[0]: r[1:] for r in self.db.execute(
            "SELECT nIndex, nReqItem1, nReqItem2, nGenRate, nOriginItem FROM ITEM_UPGRADE")}
        self.assertEqual(rows[125001], (LOW_UPGRADE, 0, 9500, 1))
        self.assertEqual(rows[125002], (700002000, LOW_UPGRADE, 5000, 2))
        self.assertEqual(rows[126001], (MIDDLE_UPGRADE, 0, 9500, 1))
        self.assertEqual(rows[126002], (700002000, MIDDLE_UPGRADE, 5000, 2))
        self.assertNotIn(125003, rows)  # the Blessed scroll has no Low Class version
        self.assertEqual(rows[120001], (HIGH_UPGRADE, 0, 9500, 1))
        self.assertEqual(len(rows), 7)

    def test_runs_again_without_changes(self):
        self.apply()
        before = [list(self.db.execute(f"SELECT * FROM {t} ORDER BY 1")) for t in ("ITEM", "ITEM_UPGRADE", "ITEM_GRADE")]
        self.apply()
        after = [list(self.db.execute(f"SELECT * FROM {t} ORDER BY 1")) for t in ("ITEM", "ITEM_UPGRADE", "ITEM_GRADE")]
        self.assertEqual(before, after)

    def test_pairs_match_the_client_names(self):
        # Against the real client tables, when the asset submodule is checked out.
        data = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "assets", "Client", "Data")
        if not os.path.exists(os.path.join(data, "Item_Org_us.tbl.csv")):
            self.skipTest("assets/Client not checked out")

        names = {item_id: name for item_id, (name, _price) in port_game_data.client_items(data).items()}
        for high, low, middle in port_game_data.SCROLL_PAIRS:
            stem = names[high].split(" (")[0].replace("Flame", "Fire")
            self.assertTrue(names[low].startswith(stem), (high, low, names[high], names[low]))
            self.assertTrue(names[middle].startswith(stem), (high, middle, names[high], names[middle]))
            self.assertIn("Low Class", names[low])
            self.assertIn("Middle Class", names[middle])


if __name__ == "__main__":
    unittest.main()
