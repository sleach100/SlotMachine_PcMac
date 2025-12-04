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
    // Note: error property may exist but be null, which is not an error
    // In JUCE, null values are represented as var(), so compare against that
    juce::var errorVar = root->getProperty("error");
    if (!errorVar.isVoid() && errorVar != juce::var() && errorVar.toString().isNotEmpty())
    {
        result.hasError = true;
        result.errorMessage = errorVar.toString();
        result.errorCode = errorVar.toString();
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
                    if (licenseObj->hasProperty("test_mode"))
                    {
                        result.testMode = licenseObj->getProperty("test_mode");
                        DBG("Test mode license detected in invalid response: " + juce::String(result.testMode ? "yes" : "no"));
                    }

                    // Handle status-specific error messages
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
                    else if (status == "inactive")
                    {
                        result.errorMessage = "License key is inactive. Please activate it in your Lemon Squeezy account.";
                        result.errorCode = "license_inactive";
                    }
                    else
                    {
                        result.errorMessage = "License validation failed (status: " + status + ").";
                        result.errorCode = "license_" + status;
                    }

                    return result;
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

                // If customer info not found in license_key.customer, check meta field
                // This is common for inactive licenses or test mode
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
            }
        }
    }

    // Extract store_id and product_id from meta for validation
    if (root->hasProperty("meta"))
    {
        juce::var metaData = root->getProperty("meta");
        if (metaData.isObject())
        {
            juce::DynamicObject* metaObj = metaData.getDynamicObject();
            if (metaObj != nullptr)
            {
                if (metaObj->hasProperty("store_id"))
                    result.storeId = metaObj->getProperty("store_id");
                if (metaObj->hasProperty("product_id"))
                    result.productId = metaObj->getProperty("product_id");

                DBG("Store ID: " + juce::String(result.storeId) + ", Product ID: " + juce::String(result.productId));
            }
        }
    }

    // Validate Store ID matches expected value
    if (result.storeId != EXPECTED_STORE_ID)
    {
        DBG("Store ID mismatch! Expected: " + juce::String(EXPECTED_STORE_ID) + ", Got: " + juce::String(result.storeId));
        result.valid = false;
        result.hasError = true;
        result.errorMessage = "This license key is not valid for this product.";
        result.errorCode = "invalid_store";
        return result;
    }

    // Validate Product ID matches expected value
    if (result.productId != EXPECTED_PRODUCT_ID)
    {
        DBG("Product ID mismatch! Expected: " + juce::String(EXPECTED_PRODUCT_ID) + ", Got: " + juce::String(result.productId));
        result.valid = false;
        result.hasError = true;
        result.errorMessage = "This license key is not valid for this product.";
        result.errorCode = "invalid_product";
        return result;
    }

    // Validate license status is "active" (not just valid)
    if (result.licenseStatus != "active")
    {
        DBG("License status is not active: " + result.licenseStatus);
        result.valid = false;
        result.hasError = true;

        if (result.licenseStatus == "inactive")
        {
            result.errorMessage = "License key is inactive. Please activate it in your Lemon Squeezy account.";
            result.errorCode = "license_inactive";
        }
        else if (result.licenseStatus == "expired")
        {
            result.errorMessage = "License key has expired.";
            result.errorCode = "license_expired";
        }
        else if (result.licenseStatus == "disabled")
        {
            result.errorMessage = "License key has been disabled.";
            result.errorCode = "license_disabled";
        }
        else
        {
            result.errorMessage = "License key status is invalid: " + result.licenseStatus;
            result.errorCode = "license_" + result.licenseStatus;
        }

        return result;
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
    // Note: error property may exist but be null, which is not an error
    juce::var errorVar = root->getProperty("error");
    bool success = errorVar.isVoid() || errorVar == juce::var() || errorVar.toString().isEmpty();
    DBG("Deactivation result (no deactivated field): " + juce::String(success ? "success" : "failed"));
    return success;
}
