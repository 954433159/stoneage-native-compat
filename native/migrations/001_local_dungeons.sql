-- The original NewFB.lua daily-count table, created empty for this instance.
CREATE TABLE IF NOT EXISTS `NewFB` (
  `Account` varchar(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
  `Name` varchar(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
  `FBdata` varchar(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  PRIMARY KEY (`Account`)
) ENGINE=InnoDB;
