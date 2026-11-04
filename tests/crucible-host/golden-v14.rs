// Copyright (C) 2026 Greg Burd.
//
// This work is open source software, licensed under the terms of the
// BSD license as described in the LICENSE file in the top-level directory.
//
// Golden-vector generator: copy to <crucible>/protocol/examples/ and run
//   cargo run -q --release -p crucible-protocol --example golden-v14
// at the commit named in golden-v14.txt; the output is golden-v14.txt.
use bytes::BytesMut;
use crucible_protocol::{CrucibleEncoder, Message};
use tokio_util::codec::Encoder;
use uuid::Uuid;

fn emit(name: &str, m: Message) {
    let mut buf = BytesMut::new();
    CrucibleEncoder::new().encode(m, &mut buf).unwrap();
    let hex: String = buf.iter().map(|b| format!("{b:02x}")).collect();
    println!("{name} {hex}");
}

fn main() {
    let up = Uuid::from_bytes(*b"\x01\x02\x03\x04\x05\x06\x47\x08\x89\x0a\x0b\x0c\x0d\x0e\x0f\x10");
    let ses = Uuid::from_bytes(*b"\x11\x12\x13\x14\x15\x16\x47\x18\x99\x1a\x1b\x1c\x1d\x1e\x1f\x20");
    let here = |version, read_only, alternate_versions| Message::HereIAm {
        version, upstairs_id: up, session_id: ses, generation: 7,
        read_only, encrypted: false, alternate_versions,
    };
    emit("here_i_am_v14_alt13", here(14, false, vec![13]));
    emit("here_i_am_v14_alt13_ro", here(14, true, vec![13]));
    emit("here_i_am_v13", here(13, false, vec![]));
    emit("been_active_please", Message::BeenActivePlease);
    emit("been_active_false", Message::BeenActive { been_active: false });
    emit("been_active_true", Message::BeenActive { been_active: true });
    emit("you_have_been_active", Message::YouHaveBeenActive { upstairs_id: up, session_id: ses });
    emit("yes_its_me_v14", Message::YesItsMe { version: 14, repair_addr: "127.0.0.1:4567".parse().unwrap() });
    emit("yes_its_me_v13", Message::YesItsMe { version: 13, repair_addr: "127.0.0.1:4567".parse().unwrap() });
    emit("version_mismatch_14", Message::VersionMismatch { version: 14 });
    emit("extent_versions_please", Message::ExtentVersionsPlease);
}
