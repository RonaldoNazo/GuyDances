#pragma once

// Returns false when another Guydances is already running (it has been asked
// to wake up). Returns true when this process owns the app.
bool try_claim_instance();
bool take_wake_request();
