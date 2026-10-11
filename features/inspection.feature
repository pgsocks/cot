Feature: Inspect semantic source values and codec losses
  Integration tools resolve canonical fields without duplicating CoT defaults.

  Scenario: Optional absence does not manufacture a mapping source value
    Given an event with Undefined access and a present empty contact
    When I inspect access, contact callsign, endpoint, and latitude
    Then access and endpoint are unavailable mapping sources
    And the empty callsign and zero latitude are available mapping sources
    And the contact group is present

  Scenario: Unknown wire fields are visible as conversion losses
    Given a Protobuf event with one unknown field
    When I decode it with structured diagnostics
    Then decoding succeeds and reports one discarded Protobuf field
    And the decoded semantic values can still be inspected

  Scenario: Missing XML fields have actionable diagnostics
    Given an XML event missing its uid attribute
    When I decode it with structured diagnostics
    Then decoding fails with a missing uid diagnostic and input location

  Scenario: Field metadata is readable without a private enum registry
    When I inspect metadata for "detail.track.speed"
    Then its type is "double", unit is "metres_per_second", and group is "detail.track"
