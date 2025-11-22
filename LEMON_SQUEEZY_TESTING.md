# Lemon Squeezy Test Mode License Guide

## Understanding Test Mode Licenses

### Key Differences Between Test and Production Licenses

**Test Mode Licenses:**
- Created in Lemon Squeezy's test mode environment
- Show as "Inactive (0/X)" in the dashboard by default
- **Cannot be manually activated through the Lemon Squeezy dashboard**
- Are meant for development and testing purposes only
- Still validate successfully through the `/licenses/validate` API endpoint
- Do not require payment or actual customer transactions

**Production Licenses:**
- Created from real customer purchases
- Can be activated and managed through the dashboard
- Track real activation limits and usage
- Require actual payment processing

### Why Test Licenses Show as "Inactive"

This is **expected behavior**. Lemon Squeezy test mode licenses:
1. Remain in "Inactive" status in the dashboard
2. Cannot be changed to "Active" status manually
3. **Still work correctly when validated through the API**

The "Inactive" status in test mode does NOT mean the license is invalid. It simply indicates these are test licenses.

## How to Test License Validation

### Step 1: Get Your Test License Keys

From your Lemon Squeezy dashboard (test mode):
1. Navigate to: Store → Products → S.L.O.T. Machine → Licenses
2. Copy one of the test license keys (e.g., `XXXX-XXXX-XXXX-XXXX`)
3. Don't worry that it shows as "Inactive" - this is normal for test licenses

### Step 2: Test the License in Your Plugin

1. Build and run the plugin in Debug mode
2. Enter the test license key in the license validation UI
3. Check the debug console for detailed output

### Step 3: Interpret the Debug Output

The updated code now provides detailed logging:

```
Lemon Squeezy API Response: {full JSON response}
License validation succeeded (valid=true)
License details - Status: inactive, Activation: 0/2
Test mode license detected: yes
License registered to: Steve Leach (steve@example.com)
```

**Success indicators:**
- `"valid": true` in the JSON response
- "License validation succeeded" message
- License details are extracted correctly

**Failure indicators:**
- `"valid": false` in the JSON response
- Specific error messages explaining why validation failed
- "License status: expired (valid=false)" for expired licenses

## Expected API Behavior

### Successful Test License Validation

Even though the license shows as "Inactive" in the dashboard, the API should return:

```json
{
  "valid": true,
  "license_key": {
    "status": "inactive",
    "key": "XXXX-XXXX-XXXX-XXXX",
    "activation_limit": 2,
    "activation_usage": 0,
    "test_mode": true,
    "customer": {
      "name": "Steve Leach",
      "email": "steve@example.com"
    }
  }
}
```

**Key points:**
- `valid: true` - The license validates successfully
- `status: "inactive"` - Normal for test licenses
- `test_mode: true` - Indicates this is a test license

### Failed Validation Examples

**Expired License:**
```json
{
  "valid": false,
  "license_key": {
    "status": "expired"
  },
  "error": "license_expired"
}
```

**Invalid License Key:**
```json
{
  "valid": false,
  "error": "license_not_found"
}
```

## Troubleshooting

### Issue: API returns `valid: false` for test license

**Possible causes:**
1. License key is incorrectly formatted or typed wrong
2. API key in the code doesn't have proper permissions
3. Network/connection issues to Lemon Squeezy API
4. The store or product is not properly configured in test mode

**Solutions:**
1. Double-check the license key (copy-paste to avoid typos)
2. Verify API key has read permissions for licenses
3. Check the full API response in debug output for specific error codes
4. Ensure you're using the test mode API endpoint

### Issue: No debug output appears

**Solutions:**
1. Make sure you're running in Debug configuration (not Release)
2. Check that the debug console/output window is visible in your IDE
3. The DBG() macro only outputs in debug builds

### Issue: "Failed to connect to Lemon Squeezy API"

**Possible causes:**
1. No internet connection
2. Firewall blocking outbound HTTPS requests
3. Lemon Squeezy API is down (rare)

**Solutions:**
1. Test internet connectivity
2. Check firewall settings
3. Verify you can access https://api.lemonsqueezy.com in a browser

## Moving to Production

When you're ready to move from test to production:

1. **Switch Lemon Squeezy to Production Mode:**
   - In your Lemon Squeezy dashboard, switch from Test Mode to Production Mode
   - This is typically a toggle in the top-right corner

2. **Update API Keys:**
   - Generate a production API key (test keys won't work in production)
   - Update the API key in `LemonSqueezyAPI.cpp`

3. **Test with Real Licenses:**
   - Create a test purchase or use a development license
   - Production licenses will show as "Active" when properly activated
   - Activation limits and usage will be accurately tracked

4. **Remove Debug Logging (Optional):**
   - In production builds, DBG() statements are automatically excluded
   - Consider adding user-friendly error messages instead of technical debug output

## Code Changes Made

The following improvements were made to handle test licenses better:

1. **Enhanced error messages** - More descriptive validation failures
2. **Detailed debug logging** - See exactly what the API returns
3. **Test mode detection** - Identifies when a test license is being used
4. **Status differentiation** - Distinguishes between "inactive" (test mode), "expired", and "disabled"

These changes are in `/Source/LemonSqueezyAPI.cpp` lines 135-226.
