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
        jsonObject->setProperty("instance_id", instanceId);
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

    // Set up HTTP headers as a single string
    juce::String headers = "Accept: application/json\r\nContent-Type: application/json\r\n";

    // Make the HTTP request
    std::unique_ptr<juce::InputStream> stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withExtraHeaders(headers)
            .withConnectionTimeoutMs(TIMEOUT_MS)
            .withNumRedirectsToFollow(5)
            .withHttpRequestCmd("POST")
            .withPostData(requestBody)
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
                    if (status == "inactive")
                    {
                        result.errorMessage = "License key is inactive.";
                        result.errorCode = "license_inactive";
                    }
                    else if (status == "expired")
                    {
                        result.errorMessage = "License key has expired.";
                        result.errorCode = "license_expired";
                    }
                }
            }
        }

        return result;
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

bool LemonSqueezyAPI::deactivateLicense(const juce::String& licenseKey,
                                         const juce::String& instanceId)
{
    // Build the API request
    juce::URL url(juce::String(API_BASE_URL) + "/licenses/activate");

    juce::String requestBody = buildValidationRequestBody(licenseKey, instanceId);

    // Set up HTTP headers as a single string
    juce::String headers = "Accept: application/json\r\nContent-Type: application/json\r\n";

    // Make the HTTP request (same endpoint handles both activation and deactivation)
    std::unique_ptr<juce::InputStream> stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withExtraHeaders(headers)
            .withConnectionTimeoutMs(TIMEOUT_MS)
            .withNumRedirectsToFollow(5)
            .withHttpRequestCmd("POST")
            .withPostData(requestBody)
    );

    if (stream == nullptr)
    {
        return false;
    }

    // Read the response
    juce::String response = stream->readEntireStreamAsString();

    if (response.isEmpty())
    {
        return false;
    }

    // Parse the response
    juce::var parsedJson = juce::JSON::parse(response);
    if (!parsedJson.isObject())
    {
        return false;
    }

    juce::DynamicObject* root = parsedJson.getDynamicObject();
    if (root == nullptr)
    {
        return false;
    }

    // Check if deactivation was successful
    // The API should return the updated instance with deactivated=true
    if (root->hasProperty("deactivated"))
    {
        return root->getProperty("deactivated");
    }

    // Also consider it successful if there's no error
    return !root->hasProperty("error");
}
