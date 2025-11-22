#include "LemonSqueezyAPI.h"

// API key storage
namespace
{
    // For simplicity, storing the API key directly
    // In production, you might want stronger obfuscation
    // API Key: eyJ0eXAiOiJKV1QiLCJhbGciOiJSUzI1NiJ9.eyJhdWQiOiI5NGQ1OWNlZi1kYmI4LTRlYTUtYjE3OC1kMjU0MGZjZDY5MTkiLCJqdGkiOiI2YTgwZWNhODFkZjAxYWYxYjNjMzRhMTU4ZDU0N2QwYWQ5OGQ2NmQ3MTBlY2RiZGJmZDE3MDg1ZGJlYTJkMDMzZGNlNmIwMTVhNDdkNDYwNyIsImlhdCI6MTc2Mzc5NjYzNC41NTc0NjMsIm5iZiI6MTc2Mzc5NjYzNC41NTc0NjYsImV4cCI6MjA3OTMyOTQzNC41NDQzODQsInN1YiI6IjU5OTQ5OTUiLCJzY29wZXMiOltdfQ.0Ijcmp30P4-6SzHtmituhw7zr0MsjFQalFk2I8j6NRgzD1iTW2W18_vAH2yjiGCvH3qkFIg6kRcHh8Et4l_ofyuzPo5zqChQS6h29oXGQcUdK52tOZYAPj5UHZw9jZFRFeI3KotF1YLOzpS0BB3W-KHVysDuOxKjNbAkOeohG2YVJ4dtqM9jF49G4qbrXi8-xUe--anatJaAyqCnk1wgMwxZn5B3-RWTQSE1lA4PsJ98d3XuMHLu51C1lfvrjyfpeYR2_D7czTFtjQDcVoSrt6i5XkRJoMIdybEdlFd5xESnID3oKgrS3rEqZdIT59MasSpcD1nU2qZ7JBjXEwCUvrqWUf_qHC9hbK1UgPoK0s_GYtT3p6q3ET1pmjSh_1Fr3CQXcbdd4_BEtgkVF-Ntio3eWilFhgWFIO6-7qpCSHjjpGmQSBh7d2x3A1WtmTEZ299_MEwxRO5jgJdy1vuvZtstn29BhTVP2V4GKh0RgAJ9j1m64O6srxUw0TvEo8yi4qkvSDx3rebBeIDCTjXPK6BC2wFJWinOg9B3wBavoVpr5uUHOucs1TffgRJUlKrO8iiJlNfK4EDncg0YHTvDdZUaTAk-Z7sF7IcNZWsA5BmW-rb69AqC-e6VdKLp2B06DMIsHsYNNT_WOnB6JKYx2SgtlVLubPEAWoksx0kMZeI
    const char API_KEY_PLAINTEXT[] = "eyJ0eXAiOiJKV1QiLCJhbGciOiJSUzI1NiJ9.eyJhdWQiOiI5NGQ1OWNlZi1kYmI4LTRlYTUtYjE3OC1kMjU0MGZjZDY5MTkiLCJqdGkiOiI2YTgwZWNhODFkZjAxYWYxYjNjMzRhMTU4ZDU0N2QwYWQ5OGQ2NmQ3MTBlY2RiZGJmZDE3MDg1ZGJlYTJkMDMzZGNlNmIwMTVhNDdkNDYwNyIsImlhdCI6MTc2Mzc5NjYzNC41NTc0NjMsIm5iZiI6MTc2Mzc5NjYzNC41NTc0NjYsImV4cCI6MjA3OTMyOTQzNC41NDQzODQsInN1YiI6IjU5OTQ5OTUiLCJzY29wZXMiOltdfQ.0Ijcmp30P4-6SzHtmituhw7zr0MsjFQalFk2I8j6NRgzD1iTW2W18_vAH2yjiGCvH3qkFIg6kRcHh8Et4l_ofyuzPo5zqChQS6h29oXGQcUdK52tOZYAPj5UHZw9jZFRFeI3KotF1YLOzpS0BB3W-KHVysDuOxKjNbAkOeohG2YVJ4dtqM9jF49G4qbrXi8-xUe--anatJaAyqCnk1wgMwxZn5B3-RWTQSE1lA4PsJ98d3XuMHLu51C1lfvrjyfpeYR2_D7czTFtjQDcVoSrt6i5XkRJoMIdybEdlFd5xESnID3oKgrS3rEqZdIT59MasSpcD1nU2qZ7JBjXEwCUvrqWUf_qHC9hbK1UgPoK0s_GYtT3p6q3ET1pmjSh_1Fr3CQXcbdd4_BEtgkVF-Ntio3eWilFhgWFIO6-7qpCSHjjpGmQSBh7d2x3A1WtmTEZ299_MEwxRO5jgJdy1vuvZtstn29BhTVP2V4GKh0RgAJ9j1m64O6srxUw0TvEo8yi4qkvSDx3rebBeIDCTjXPK6BC2wFJWinOg9B3wBavoVpr5uUHOucs1TffgRJUlKrO8iiJlNfK4EDncg0YHTvDdZUaTAk-Z7sF7IcNZWsA5BmW-rb69AqC-e6VdKLp2B06DMIsHsYNNT_WOnB6JKYx2SgtlVLubPEAWoksx0kMZeI";

    juce::String deobfuscateKey()
    {
        // Simple version - just return the key
        // In a more secure implementation, you would XOR encode this at compile time
        return juce::String(API_KEY_PLAINTEXT);
    }
}

juce::String LemonSqueezyAPI::getAPIKey()
{
    return deobfuscateKey();
}

juce::String LemonSqueezyAPI::buildValidationRequestBody(const juce::String& licenseKey,
                                                          const juce::String& instanceId)
{
    juce::DynamicObject::Ptr jsonObject = new juce::DynamicObject();
    jsonObject->setProperty("license_key", licenseKey);

    if (instanceId.isNotEmpty())
    {
        // Lemon Squeezy expects "instance_name" not "instance_id"
        jsonObject->setProperty("instance_name", instanceId);
    }

    return juce::JSON::toString(juce::var(jsonObject.get()));
}

LicenseValidationResult LemonSqueezyAPI::validateLicense(const juce::String& licenseKey,
                                                          const juce::String& instanceId)
{
    LicenseValidationResult result;
    result.licenseKey = licenseKey;
    result.instanceId = instanceId;
    result.validatedAt = juce::Time::getCurrentTime();

    // Build the API request
    juce::URL url(juce::String(API_BASE_URL) + "/licenses/validate");

    juce::String requestBody = buildValidationRequestBody(licenseKey, instanceId);

    // Set up HTTP headers including Authorization
    juce::String apiKey = getAPIKey();
    juce::String headers = "Accept: application/json\r\n";
    headers += "Content-Type: application/json\r\n";
    headers += "Authorization: Bearer " + apiKey + "\r\n";

    // Add POST data to URL (older JUCE API)
    juce::URL postUrl = url.withPOSTData(requestBody);

    // Make the HTTP request
    std::unique_ptr<juce::InputStream> stream = postUrl.createInputStream(
        false,                      // usePostCommand = false (already set by withPOSTData)
        nullptr,                    // progressCallback
        nullptr,                    // progressCallbackContext
        headers,                    // extraHeaders
        TIMEOUT_MS,                 // timeOutMs
        nullptr,                    // responseHeaders
        nullptr,                    // numRedirectsToFollow (use default)
        0                           // httpStatusCode
    );

    if (stream == nullptr)
    {
        result.hasError = true;
        result.errorMessage = "Failed to connect to Lemon Squeezy API. Please check your internet connection.";
        return result;
    }

    // Read the response
    juce::String response = stream->readEntireStreamAsString();

    if (response.isEmpty())
    {
        result.hasError = true;
        result.errorMessage = "Received empty response from Lemon Squeezy API.";
        return result;
    }

    // Debug: Log the API response
    DBG("Lemon Squeezy API Response: " + response);

    // Store raw response for caching
    result.rawJsonResponse = response;

    // Parse the response
    return parseValidationResponse(response);
}

LicenseValidationResult LemonSqueezyAPI::parseValidationResponse(const juce::String& jsonResponse)
{
    LicenseValidationResult result;
    result.rawJsonResponse = jsonResponse;

    // Parse JSON
    juce::var parsedJson = juce::JSON::parse(jsonResponse);

    if (!parsedJson.isObject())
    {
        result.hasError = true;
        result.errorMessage = "Invalid JSON response from API.";
        return result;
    }

    juce::DynamicObject* root = parsedJson.getDynamicObject();
    if (root == nullptr)
    {
        result.hasError = true;
        result.errorMessage = "Failed to parse API response.";
        return result;
    }

    // Check if there's an error in the response
    if (root->hasProperty("error"))
    {
        result.hasError = true;
        result.errorMessage = root->getProperty("error").toString();
        result.errorCode = root->getProperty("error").toString();
        return result;
    }

    // Check if license is valid
    result.valid = root->getProperty("valid");

    if (!result.valid)
    {
        result.hasError = true;
        result.errorMessage = "License key is not valid.";

        // Check for specific error codes
        if (root->hasProperty("error"))
        {
            result.errorCode = root->getProperty("error").toString();
            result.errorMessage = "Validation failed: " + result.errorCode;
            DBG("License validation failed with error: " + result.errorCode);
        }
        else if (root->hasProperty("license_key"))
        {
            juce::var licenseKeyData = root->getProperty("license_key");
            if (licenseKeyData.isObject())
            {
                juce::DynamicObject* licenseObj = licenseKeyData.getDynamicObject();
                if (licenseObj && licenseObj->hasProperty("status"))
                {
                    juce::String status = licenseObj->getProperty("status").toString();
                    result.licenseStatus = status;  // Store status even when valid=false
                    DBG("License status: " + status + " (valid=false)");

                    // Check if this is a test mode license
                    bool isTestMode = false;
                    if (licenseObj->hasProperty("test_mode"))
                    {
                        isTestMode = licenseObj->getProperty("test_mode");
                        result.testMode = isTestMode;
                        DBG("Test mode license detected in invalid response: " + juce::String(isTestMode ? "yes" : "no"));
                    }

                    // For test mode licenses with status "inactive", extract the license data
                    // even though the API returns valid=false
                    if (isTestMode && status == "inactive")
                    {
                        DBG("Test mode inactive license - extracting license data despite valid=false");

                        // Extract license key and status
                        result.licenseKey = licenseObj->getProperty("key").toString();
                        result.activationLimit = licenseObj->getProperty("activation_limit");
                        result.activationUsage = licenseObj->getProperty("activation_usage");

                        // Extract customer info
                        if (licenseObj->hasProperty("customer"))
                        {
                            juce::var customerData = licenseObj->getProperty("customer");
                            if (customerData.isObject())
                            {
                                juce::DynamicObject* customerObj = customerData.getDynamicObject();
                                if (customerObj != nullptr)
                                {
                                    result.licenseeEmail = customerObj->getProperty("email").toString();
                                    result.licenseeName = customerObj->getProperty("name").toString();
                                }
                            }
                        }

                        // If customer info not found in license_key.customer, check meta field
                        if (result.licenseeEmail.isEmpty() || result.licenseeName.isEmpty())
                        {
                            DBG("Customer info not found in license_key.customer, checking meta field");
                            if (root->hasProperty("meta"))
                            {
                                juce::var metaData = root->getProperty("meta");
                                if (metaData.isObject())
                                {
                                    juce::DynamicObject* metaObj = metaData.getDynamicObject();
                                    if (metaObj != nullptr)
                                    {
                                        if (metaObj->hasProperty("customer_email"))
                                            result.licenseeEmail = metaObj->getProperty("customer_email").toString();
                                        if (metaObj->hasProperty("customer_name"))
                                            result.licenseeName = metaObj->getProperty("customer_name").toString();

                                        DBG("Extracted from meta - Name: " + result.licenseeName + ", Email: " + result.licenseeEmail);
                                    }
                                }
                            }
                        }

                        // If we successfully extracted customer info, treat as valid
                        if (result.licenseeName.isNotEmpty() && result.licenseeEmail.isNotEmpty())
                        {
                            DBG("Test mode license has valid customer data - treating as valid");
                            result.valid = true;
                            result.hasError = false;
                            result.errorMessage = "";
                            result.errorCode = "";

                            // Continue parsing to extract instance info below
                            // Don't return early
                        }
                        else
                        {
                            DBG("Test mode license missing customer data - treating as invalid");
                            result.errorMessage = "Test mode license is missing customer information.";
                            result.errorCode = "license_incomplete";
                            return result;
                        }
                    }
                    else
                    {
                        // Not a test mode inactive license - handle normally
                        // Note: Test mode licenses may show as "inactive" but still validate successfully
                        // Only reject if the status is explicitly problematic
                        if (status == "expired")
                        {
                            result.errorMessage = "License key has expired.";
                            result.errorCode = "license_expired";
                        }
                        else if (status == "disabled")
                        {
                            result.errorMessage = "License key has been disabled.";
                            result.errorCode = "license_disabled";
                        }
                        else
                        {
                            result.errorMessage = "License validation failed (status: " + status + ").";
                            result.errorCode = "license_" + status;
                        }

                        return result;
                    }
                }
                else
                {
                    return result;
                }
            }
            else
            {
                return result;
            }
        }
        else
        {
            return result;
        }
    }
    else
    {
        DBG("License validation succeeded (valid=true)");
    }

    // Extract license information
    if (root->hasProperty("license_key"))
    {
        juce::var licenseKeyData = root->getProperty("license_key");
        if (licenseKeyData.isObject())
        {
            juce::DynamicObject* licenseObj = licenseKeyData.getDynamicObject();
            if (licenseObj != nullptr)
            {
                result.licenseKey = licenseObj->getProperty("key").toString();
                result.licenseStatus = licenseObj->getProperty("status").toString();
                result.activationLimit = licenseObj->getProperty("activation_limit");
                result.activationUsage = licenseObj->getProperty("activation_usage");

                DBG("License details - Status: " + result.licenseStatus +
                    ", Activation: " + juce::String(result.activationUsage) + "/" +
                    juce::String(result.activationLimit));

                // Check if this is a test mode license
                if (licenseObj->hasProperty("test_mode"))
                {
                    bool testMode = licenseObj->getProperty("test_mode");
                    result.testMode = testMode;
                    DBG("Test mode license detected: " + juce::String(testMode ? "yes" : "no"));
                }

                // Extract customer info
                if (licenseObj->hasProperty("customer"))
                {
                    juce::var customerData = licenseObj->getProperty("customer");
                    if (customerData.isObject())
                    {
                        juce::DynamicObject* customerObj = customerData.getDynamicObject();
                        if (customerObj != nullptr)
                        {
                            result.licenseeEmail = customerObj->getProperty("email").toString();
                            result.licenseeName = customerObj->getProperty("name").toString();
                            DBG("License registered to: " + result.licenseeName + " (" + result.licenseeEmail + ")");
                        }
                    }
                }
            }
        }
    }

    // Extract instance information
    if (root->hasProperty("instance"))
    {
        juce::var instanceData = root->getProperty("instance");
        if (instanceData.isObject())
        {
            juce::DynamicObject* instanceObj = instanceData.getDynamicObject();
            if (instanceObj != nullptr)
            {
                result.instanceId = instanceObj->getProperty("name").toString();
            }
        }
    }

    // Check for activation limit errors
    if (result.activationLimit > 0 && result.activationUsage >= result.activationLimit)
    {
        // This might happen if the validation succeeded but we're at the limit
        // Usually Lemon Squeezy will return valid=false in this case, but check anyway
        if (root->hasProperty("meta"))
        {
            juce::var metaData = root->getProperty("meta");
            if (metaData.isObject())
            {
                juce::DynamicObject* metaObj = metaData.getDynamicObject();
                if (metaObj != nullptr && metaObj->hasProperty("store_id"))
                {
                    // Validation succeeded, we're just at the limit but this instance is already activated
                    // This is OK
                }
            }
        }
    }

    return result;
}

LicenseValidationResult LemonSqueezyAPI::parseCachedResponse(const juce::String& jsonResponse)
{
    // Reuse the same parsing logic
    return parseValidationResponse(jsonResponse);
}

LicenseValidationResult LemonSqueezyAPI::activateLicense(const juce::String& licenseKey,
                                                          const juce::String& instanceId)
{
    LicenseValidationResult result;
    result.licenseKey = licenseKey;
    result.instanceId = instanceId;
    result.validatedAt = juce::Time::getCurrentTime();

    DBG("Attempting to activate license: " + licenseKey);
    DBG("Instance ID: " + instanceId);

    // Build the API request
    juce::URL url(juce::String(API_BASE_URL) + "/licenses/activate");

    juce::String requestBody = buildValidationRequestBody(licenseKey, instanceId);

    // Set up HTTP headers including Authorization
    juce::String apiKey = getAPIKey();
    juce::String headers = "Accept: application/json\r\n";
    headers += "Content-Type: application/json\r\n";
    headers += "Authorization: Bearer " + apiKey + "\r\n";

    // Add POST data to URL (older JUCE API)
    juce::URL postUrl = url.withPOSTData(requestBody);

    // Make the HTTP request
    std::unique_ptr<juce::InputStream> stream = postUrl.createInputStream(
        false,                      // usePostCommand = false (already set by withPOSTData)
        nullptr,                    // progressCallback
        nullptr,                    // progressCallbackContext
        headers,                    // extraHeaders
        TIMEOUT_MS,                 // timeOutMs
        nullptr,                    // responseHeaders
        nullptr,                    // numRedirectsToFollow (use default)
        0                           // httpStatusCode
    );

    if (stream == nullptr)
    {
        result.hasError = true;
        result.errorMessage = "Failed to connect to Lemon Squeezy API for activation.";
        DBG("Activation failed: Could not connect to API");
        return result;
    }

    // Read the response
    juce::String response = stream->readEntireStreamAsString();

    if (response.isEmpty())
    {
        result.hasError = true;
        result.errorMessage = "Received empty response from Lemon Squeezy activation API.";
        DBG("Activation failed: Empty response");
        return result;
    }

    // Debug: Log the API response
    DBG("Lemon Squeezy Activation Response: " + response);

    // Store raw response
    result.rawJsonResponse = response;

    // Parse the response - activation returns the same structure as validation
    return parseValidationResponse(response);
}

bool LemonSqueezyAPI::deactivateLicense(const juce::String& licenseKey,
                                         const juce::String& instanceId)
{
    DBG("Attempting to deactivate license: " + licenseKey);
    DBG("Instance ID: " + instanceId);

    // Build the API request
    juce::URL url(juce::String(API_BASE_URL) + "/licenses/deactivate");

    juce::String requestBody = buildValidationRequestBody(licenseKey, instanceId);

    // Set up HTTP headers including Authorization
    juce::String apiKey = getAPIKey();
    juce::String headers = "Accept: application/json\r\n";
    headers += "Content-Type: application/json\r\n";
    headers += "Authorization: Bearer " + apiKey + "\r\n";

    // Add POST data to URL (older JUCE API)
    juce::URL postUrl = url.withPOSTData(requestBody);

    // Make the HTTP request (same endpoint handles both activation and deactivation)
    std::unique_ptr<juce::InputStream> stream = postUrl.createInputStream(
        false,                      // usePostCommand = false (already set by withPOSTData)
        nullptr,                    // progressCallback
        nullptr,                    // progressCallbackContext
        headers,                    // extraHeaders
        TIMEOUT_MS,                 // timeOutMs
        nullptr,                    // responseHeaders
        nullptr,                    // numRedirectsToFollow (use default)
        0                           // httpStatusCode
    );

    if (stream == nullptr)
    {
        DBG("Deactivation failed: Could not connect to API");
        return false;
    }

    // Read the response
    juce::String response = stream->readEntireStreamAsString();

    if (response.isEmpty())
    {
        DBG("Deactivation failed: Empty response");
        return false;
    }

    // Debug: Log the API response
    DBG("Lemon Squeezy Deactivation Response: " + response);

    // Parse the response
    juce::var parsedJson = juce::JSON::parse(response);
    if (!parsedJson.isObject())
    {
        DBG("Deactivation failed: Invalid JSON response");
        return false;
    }

    juce::DynamicObject* root = parsedJson.getDynamicObject();
    if (root == nullptr)
    {
        DBG("Deactivation failed: Could not parse response");
        return false;
    }

    // Check if deactivation was successful
    // The API should return the updated instance with deactivated=true
    if (root->hasProperty("deactivated"))
    {
        bool success = root->getProperty("deactivated");
        DBG("Deactivation result: " + juce::String(success ? "success" : "failed"));
        return success;
    }

    // Also consider it successful if there's no error
    bool success = !root->hasProperty("error");
    DBG("Deactivation result (no deactivated field): " + juce::String(success ? "success" : "failed"));
    return success;
}
