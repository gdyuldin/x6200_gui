.separator "\t"

CREATE TABLE bands(
    id          INTEGER PRIMARY KEY,
    name        TEXT,
    start_freq  INTEGER,
    stop_freq   INTEGER,
    type        INTEGER -- stop on band up/down buttons
);

.import bands_ham.csv bands
.import bands_cb.csv bands
.import bands_broadcast.csv bands
.import bands_transverter.csv bands

CREATE TABLE band_params(
    bands_id    INTEGER,
    name        TEXT,
    val         INTEGER,
    UNIQUE      (bands_id, name) ON CONFLICT REPLACE
);

.import band_params.csv band_params

CREATE TABLE params(
    name        TEXT PRIMARY KEY ON CONFLICT REPLACE,
    val         INTEGER
);

.import params.csv params

CREATE TABLE mode_params(
    mode        INTEGER,
    name        TEXT,
    val         INTEGER,
    UNIQUE      (mode, name) ON CONFLICT REPLACE
);

.import mode_params.csv mode_params

CREATE TABLE atu(
    ant         INTEGER,
    freq        INTEGER,
    val         INTEGER,
    UNIQUE      (ant, freq) ON CONFLICT REPLACE
);

CREATE TABLE memory(
    id          INTEGER,
    name        TEXT,
    val         INTEGER,
    UNIQUE      (id, name) ON CONFLICT REPLACE
);

CREATE TABLE msg_cw(
    id          INTEGER PRIMARY KEY,
    val         INTEGER
);

CREATE TABLE transverter(
    id          INTEGER,
    name        TEXT,
    val         INTEGER,
    UNIQUE      (id, name) ON CONFLICT REPLACE
);

CREATE TABLE version(id INT NOT NULL DEFAULT 0);
INSERT INTO version(id) values (1);

CREATE TABLE digital_modes(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    label varchar(64) NOT NULL,
    freq INTEGER NOT NULL CHECK(freq > 0),
    mode INTEGER NOT NULL DEFAULT 3 CHECK(mode >= 0 AND mode <= 7),
    type INTEGER NOT NULL,
    CONSTRAINT freq_type_uniq UNIQUE(freq, type)
);
CREATE INDEX digital_modes_type_idx ON digital_modes (type);
CREATE INDEX digital_modes_freq_idx ON digital_modes (freq);

.separator ","
.import digital_modes.csv digital_modes

