// Runs before the components (first import in main.ts): picks up the
// credentials from the device's QR link (http://<device>/#fbcred=...) before
// the IDE sends its first request, and clears them from the address bar.
import { importCredentialsFromLocation } from './auth.js';

importCredentialsFromLocation();
